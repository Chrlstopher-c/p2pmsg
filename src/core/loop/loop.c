/* core/loop : epoll mono-thread, timers (tas + un seul timerfd), réveil inter-thread par eventfd.
 * Fd retiré pendant une itération : marqué dead, libéré en fin d'itération (graveyard). */
#define _GNU_SOURCE
#include "core/loop/loop_internal.h"
#include "core/log/log.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define LOOP_MOD "loop"
#define EV_BATCH 64
#define NS_PER_MS 1000000u

typedef struct fd_entry {
    int fd;
    uint32_t events;
    int dead;
    loop_io_cb cb;
    void *ud;
    struct fd_entry *next;
    struct fd_entry *gnext;
} fd_entry;

struct loop {
    int ep;
    int tfd;
    int efd;
    int stop;
    uint64_t now;
    uint64_t armed;
    uint64_t next_id;
    timer_heap timers;
    fd_entry *fds;
    fd_entry *graveyard;
    loop_wake_cb wake_cb;
    void *wake_ud;
    char tag_timer;
    char tag_wake;
};

static uint64_t mono_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        LOG_E(LOOP_MOD, "clock_gettime errno=%d", errno);
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / NS_PER_MS;
}

static uint32_t to_epoll(uint32_t events)
{
    uint32_t e = EPOLLRDHUP;

    if (events & LOOP_READ) { e |= EPOLLIN; }
    if (events & LOOP_WRITE) { e |= EPOLLOUT; }
    return e;
}

static uint32_t to_mask(uint32_t ev)
{
    uint32_t m = 0;

    if (ev & EPOLLIN) { m |= LOOP_READ; }
    if (ev & EPOLLOUT) { m |= LOOP_WRITE; }
    if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) { m |= LOOP_ERROR; }
    return m;
}

static void drain_fd(int fd)
{
    uint64_t v;

    if (read(fd, &v, sizeof v) < 0 && errno != EAGAIN) {
        LOG_W(LOOP_MOD, "drain fd=%d errno=%d", fd, errno);
    }
}

static void close_if_open(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

static void free_fd_list(fd_entry *e)
{
    while (e) {
        fd_entry *n = e->next;

        free(e);
        e = n;
    }
}

static void free_graveyard(loop_t *l)
{
    fd_entry *g = l->graveyard;

    while (g) {
        fd_entry *n = g->gnext;

        free(g);
        g = n;
    }
    l->graveyard = NULL;
}

/* Arme le timerfd sur l'échéance la plus proche ; désarmé sans timer (epoll bloque sans timeout). */
static int arm_timer(loop_t *l)
{
    timer_node *top = timer_heap_top_live(&l->timers);
    uint64_t want = top ? top->deadline : 0;
    struct itimerspec its = {0};
    uint64_t delta;

    if (want == l->armed) {
        return 0;
    }
    if (top) {
        delta = want > l->now ? want - l->now : 0;
        its.it_value.tv_sec = (time_t)(delta / 1000u);
        its.it_value.tv_nsec = (long)((delta % 1000u) * NS_PER_MS);
        if (delta == 0) {
            its.it_value.tv_nsec = 1;
        }
    }
    if (timerfd_settime(l->tfd, 0, &its, NULL) != 0) {
        LOG_E(LOOP_MOD, "timerfd_settime errno=%d", errno);
        return -errno;
    }
    l->armed = want;
    return 0;
}

static void fire_expired(loop_t *l)
{
    timer_heap *h = &l->timers;

    l->now = mono_ms();
    while (h->len > 0 && h->items[0].deadline <= l->now) {
        timer_node n = h->items[0];

        timer_heap_pop(h);
        if (n.cancelled) {
            continue;
        }
        if (n.repeat > 0) {
            n.deadline = l->now + n.repeat;
            if (timer_heap_push(h, n) != 0) {
                LOG_E(LOOP_MOD, "timer repeat dropped id=%llu", (unsigned long long)n.id);
            }
        }
        n.cb(l, n.id, n.ud);
    }
}

static fd_entry *find_fd(loop_t *l, int fd, fd_entry **prev_out)
{
    fd_entry *prev = NULL;
    fd_entry *e = l->fds;

    while (e && e->fd != fd) {
        prev = e;
        e = e->next;
    }
    if (prev_out) {
        *prev_out = prev;
    }
    return e;
}

static void dispatch_event(loop_t *l, const struct epoll_event *ev)
{
    fd_entry *e = ev->data.ptr;

    if (ev->data.ptr == &l->tag_timer) {
        drain_fd(l->tfd);
        l->armed = 0;
    } else if (ev->data.ptr == &l->tag_wake) {
        drain_fd(l->efd);
        if (l->wake_cb) {
            l->wake_cb(l, l->wake_ud);
        }
    } else if (!e->dead) {
        e->cb(l, e->fd, to_mask(ev->events), e->ud);
    }
}

static int ep_register(loop_t *l, int fd, void *tag)
{
    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = tag };

    return epoll_ctl(l->ep, EPOLL_CTL_ADD, fd, &ev) == 0 ? 0 : -errno;
}

static int init_fds(loop_t *l)
{
    l->ep = epoll_create1(EPOLL_CLOEXEC);
    l->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    l->efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (l->ep < 0 || l->tfd < 0 || l->efd < 0) {
        return -errno;
    }
    if (ep_register(l, l->tfd, &l->tag_timer) != 0) {
        return -errno;
    }
    return ep_register(l, l->efd, &l->tag_wake);
}

loop_t *loop_new(void)
{
    loop_t *l = calloc(1, sizeof *l);
    int rc;

    if (!l) {
        return NULL;
    }
    l->ep = l->tfd = l->efd = -1;
    rc = init_fds(l);
    if (rc != 0) {
        LOG_E(LOOP_MOD, "init failed errno=%d", -rc);
        loop_free(l);
        return NULL;
    }
    l->now = mono_ms();
    l->next_id = 1;
    return l;
}

void loop_free(loop_t *l)
{
    if (!l) {
        return;
    }
    close_if_open(l->ep);
    close_if_open(l->tfd);
    close_if_open(l->efd);
    free_fd_list(l->fds);
    free_graveyard(l);
    timer_heap_free(&l->timers);
    free(l);
}

int loop_add_fd(loop_t *l, int fd, uint32_t events, loop_io_cb cb, void *ud)
{
    fd_entry *e;
    struct epoll_event ev;

    if (!l || fd < 0 || !cb) {
        return -EINVAL;
    }
    e = calloc(1, sizeof *e);
    if (!e) {
        return -ENOMEM;
    }
    e->fd = fd;
    e->events = events;
    e->cb = cb;
    e->ud = ud;
    ev.events = to_epoll(events);
    ev.data.ptr = e;
    if (epoll_ctl(l->ep, EPOLL_CTL_ADD, fd, &ev) != 0) {
        int err = errno;

        free(e);
        LOG_W(LOOP_MOD, "add fd=%d errno=%d", fd, err);
        return -err;
    }
    e->next = l->fds;
    l->fds = e;
    return 0;
}

int loop_mod_fd(loop_t *l, int fd, uint32_t events)
{
    fd_entry *e;
    struct epoll_event ev;

    if (!l) {
        return -EINVAL;
    }
    e = find_fd(l, fd, NULL);
    if (!e) {
        return -ENOENT;
    }
    ev.events = to_epoll(events);
    ev.data.ptr = e;
    if (epoll_ctl(l->ep, EPOLL_CTL_MOD, fd, &ev) != 0) {
        return -errno;
    }
    e->events = events;
    return 0;
}

int loop_del_fd(loop_t *l, int fd)
{
    fd_entry *prev = NULL;
    fd_entry *e;

    if (!l) {
        return -EINVAL;
    }
    e = find_fd(l, fd, &prev);
    if (!e) {
        return -ENOENT;
    }
    if (epoll_ctl(l->ep, EPOLL_CTL_DEL, fd, NULL) != 0) {
        LOG_D(LOOP_MOD, "del fd=%d errno=%d", fd, errno);
    }
    if (prev) {
        prev->next = e->next;
    } else {
        l->fds = e->next;
    }
    e->dead = 1;
    e->gnext = l->graveyard;
    l->graveyard = e;
    return 0;
}

uint64_t loop_timer_add(loop_t *l, uint64_t delay_ms, uint64_t repeat_ms, loop_timer_cb cb, void *ud)
{
    timer_node n;

    if (!l || !cb) {
        return 0;
    }
    l->now = mono_ms();
    n.deadline = l->now + delay_ms;
    n.id = l->next_id++;
    n.repeat = repeat_ms;
    n.cb = cb;
    n.ud = ud;
    n.cancelled = 0;
    if (timer_heap_push(&l->timers, n) != 0) {
        return 0;
    }
    return n.id;
}

void loop_timer_cancel(loop_t *l, uint64_t id)
{
    if (l && id != 0) {
        timer_heap_cancel(&l->timers, id);
    }
}

int loop_set_wake(loop_t *l, loop_wake_cb cb, void *ud)
{
    if (!l) {
        return -EINVAL;
    }
    l->wake_cb = cb;
    l->wake_ud = ud;
    return 0;
}

int loop_wake(loop_t *l)
{
    uint64_t one = 1;
    ssize_t r;

    if (!l) {
        return -EINVAL;
    }
    r = write(l->efd, &one, sizeof one);
    if (r == (ssize_t)sizeof one || (r < 0 && errno == EAGAIN)) {
        return 0;
    }
    return -errno;
}

uint64_t loop_now_ms(const loop_t *l)
{
    return l->now;
}

int loop_run_once(loop_t *l, int timeout_ms)
{
    struct epoll_event evs[EV_BATCH];
    int n;
    int rc;

    if (!l) {
        return -EINVAL;
    }
    rc = arm_timer(l);
    if (rc != 0) {
        return rc;
    }
    n = epoll_wait(l->ep, evs, EV_BATCH, timeout_ms);
    if (n < 0) {
        if (errno != EINTR) {
            LOG_E(LOOP_MOD, "epoll_wait errno=%d", errno);
            return -errno;
        }
        n = 0;
    }
    l->now = mono_ms();
    for (int i = 0; i < n; ++i) {
        dispatch_event(l, &evs[i]);
    }
    fire_expired(l);
    free_graveyard(l);
    return n;
}

int loop_run(loop_t *l)
{
    int r;

    if (!l) {
        return -EINVAL;
    }
    l->stop = 0;
    while (!l->stop) {
        r = loop_run_once(l, -1);
        if (r < 0) {
            return r;
        }
    }
    return 0;
}

void loop_stop(loop_t *l)
{
    if (l) {
        l->stop = 1;
    }
}
