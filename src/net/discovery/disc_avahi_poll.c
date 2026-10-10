/* net/discovery — AvahiPoll maison sur core/loop : fd -> loop_add/mod/del_fd, timeouts -> loop_timer_*.
 * Avahi fournit des temps absolus (gettimeofday) ; converti en delai relatif pour loop_timer_add. */
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include "core/log/log.h"
#include "net/discovery/disc_internal.h"

struct AvahiWatch {
    loop_t           *loop;
    int               fd;
    AvahiWatchCallback cb;
    void             *ud;
    AvahiWatchEvent   revents;
};

struct AvahiTimeout {
    loop_t              *loop;
    uint64_t             timer;
    AvahiTimeoutCallback cb;
    void                *ud;
};

static uint32_t to_loop_events(AvahiWatchEvent ev)
{
    uint32_t out = 0;

    if (ev & AVAHI_WATCH_IN) {
        out |= LOOP_READ;
    }
    if (ev & AVAHI_WATCH_OUT) {
        out |= LOOP_WRITE;
    }
    return out;
}

static AvahiWatchEvent from_loop_events(uint32_t ev)
{
    int out = 0;

    if (ev & LOOP_READ) {
        out |= AVAHI_WATCH_IN;
    }
    if (ev & LOOP_WRITE) {
        out |= AVAHI_WATCH_OUT;
    }
    if (ev & LOOP_ERROR) {
        out |= AVAHI_WATCH_ERR | AVAHI_WATCH_HUP;
    }
    return (AvahiWatchEvent)out;
}

static void io_cb(loop_t *l, int fd, uint32_t events, void *ud)
{
    AvahiWatch *w = ud;

    (void)l;
    (void)fd;
    w->revents = from_loop_events(events);
    w->cb(w, w->fd, w->revents, w->ud);
}

static AvahiWatch *poll_watch_new(const AvahiPoll *api, int fd, AvahiWatchEvent ev,
                                  AvahiWatchCallback cb, void *ud)
{
    loop_t *l = api->userdata;
    AvahiWatch *w = calloc(1, sizeof *w);
    int rc;

    if (!w) {
        LOG_E(DISC_LOG, "avahi watch: allocation impossible");
        return NULL;
    }
    w->loop = l;
    w->fd = fd;
    w->cb = cb;
    w->ud = ud;
    rc = loop_add_fd(l, fd, to_loop_events(ev), io_cb, w);
    if (rc != 0) {
        LOG_W(DISC_LOG, "avahi watch: loop_add_fd fd=%d rc=%d", fd, rc);
        free(w);
        return NULL;
    }
    return w;
}

static void poll_watch_update(AvahiWatch *w, AvahiWatchEvent ev)
{
    int rc = loop_mod_fd(w->loop, w->fd, to_loop_events(ev));

    if (rc != 0) {
        LOG_W(DISC_LOG, "avahi watch: loop_mod_fd fd=%d rc=%d", w->fd, rc);
    }
}

static AvahiWatchEvent poll_watch_get_events(AvahiWatch *w)
{
    return w->revents;
}

static void poll_watch_free(AvahiWatch *w)
{
    int rc = loop_del_fd(w->loop, w->fd);

    if (rc != 0 && rc != -ENOENT) {
        LOG_W(DISC_LOG, "avahi watch: loop_del_fd fd=%d rc=%d", w->fd, rc);
    }
    free(w);
}

static uint64_t delay_ms_until(const struct timeval *tv)
{
    struct timeval now;
    int64_t ms;

    gettimeofday(&now, NULL);
    ms = (int64_t)(tv->tv_sec - now.tv_sec) * 1000 + (int64_t)(tv->tv_usec - now.tv_usec) / 1000;
    return ms > 0 ? (uint64_t)ms : 0;
}

static void timeout_timer_cb(loop_t *l, uint64_t id, void *ud)
{
    AvahiTimeout *t = ud;

    (void)l;
    (void)id;
    t->timer = 0;
    t->cb(t, t->ud);
}

static void timeout_arm(AvahiTimeout *t, const struct timeval *tv)
{
    if (t->timer != 0) {
        loop_timer_cancel(t->loop, t->timer);
        t->timer = 0;
    }
    if (!tv) {
        return;
    }
    t->timer = loop_timer_add(t->loop, delay_ms_until(tv), 0, timeout_timer_cb, t);
    if (t->timer == 0) {
        LOG_E(DISC_LOG, "avahi timeout: loop_timer_add impossible");
    }
}

static AvahiTimeout *poll_timeout_new(const AvahiPoll *api, const struct timeval *tv,
                                      AvahiTimeoutCallback cb, void *ud)
{
    AvahiTimeout *t = calloc(1, sizeof *t);

    if (!t) {
        LOG_E(DISC_LOG, "avahi timeout: allocation impossible");
        return NULL;
    }
    t->loop = api->userdata;
    t->cb = cb;
    t->ud = ud;
    timeout_arm(t, tv);
    return t;
}

static void poll_timeout_update(AvahiTimeout *t, const struct timeval *tv)
{
    timeout_arm(t, tv);
}

static void poll_timeout_free(AvahiTimeout *t)
{
    if (t->timer != 0) {
        loop_timer_cancel(t->loop, t->timer);
    }
    free(t);
}

void disc_poll_init(AvahiPoll *api, loop_t *l)
{
    api->userdata = l;
    api->watch_new = poll_watch_new;
    api->watch_update = poll_watch_update;
    api->watch_get_events = poll_watch_get_events;
    api->watch_free = poll_watch_free;
    api->timeout_new = poll_timeout_new;
    api->timeout_update = poll_timeout_update;
    api->timeout_free = poll_timeout_free;
}
