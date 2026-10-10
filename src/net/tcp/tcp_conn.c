#define _GNU_SOURCE
/* net/tcp : cycle de vie d'une connexion (connect, rx/tx bornés, fermeture différée de on_close). */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "core/log/log.h"
#include "net/tcp/tcp_internal.h"

#define TCP_MOD "net/tcp"
#define TCP_READ_CHUNK 16384u

static uint32_t events_for(const tcp_conn_t *c)
{
    if (c->connecting) {
        return LOOP_WRITE;
    }
    return LOOP_READ | (c->want_write ? LOOP_WRITE : 0u);
}

static void sync_events(tcp_conn_t *c)
{
    uint32_t want = events_for(c);

    if (c->finalize_scheduled || want == c->events) {
        return;
    }
    if (loop_mod_fd(c->loop, c->fd, want) == 0) {
        c->events = want;
    }
}

static void finalize_cb(loop_t *l, uint64_t timer_id, void *ud);

static void schedule_finalize(tcp_conn_t *c)
{
    if (c->finalize_scheduled) {
        return;
    }
    c->finalize_scheduled = 1;
    loop_del_fd(c->loop, c->fd);
    if (loop_timer_add(c->loop, 0, 0, finalize_cb, c) == 0) {
        LOG_E(TCP_MOD, "timer de fermeture indisponible, libération immédiate");
        finalize_cb(c->loop, 0, c);
    }
}

static void conn_fail(tcp_conn_t *c, int err)
{
    if (c->finalize_scheduled) {
        return;
    }
    c->err = err;
    schedule_finalize(c);
}

static void finalize_cb(loop_t *l, uint64_t timer_id, void *ud)
{
    tcp_conn_t *c = (tcp_conn_t *)ud;

    (void)l;
    (void)timer_id;
    if (c->fd >= 0) {
        close(c->fd);
    }
    if (c->has_handlers && c->handlers.on_close != NULL) {
        c->handlers.on_close(c, c->err, c->ud);
    }
    buf_free(&c->rx);
    buf_free(&c->tx);
    free(c);
}

static void deliver_rx(tcp_conn_t *c)
{
    if (c->closing || c->finalize_scheduled || buf_readable(&c->rx) == 0) {
        return;
    }
    if (c->has_handlers && c->handlers.on_data != NULL) {
        c->handlers.on_data(c, &c->rx, c->ud);
    }
    if (buf_readable(&c->rx) == 0) {
        buf_reset(&c->rx);
    }
}

static int read_chunk_into_rx(tcp_conn_t *c, uint8_t *chunk, size_t room)
{
    ssize_t n = recv(c->fd, chunk, room, 0);

    if (n > 0) {
        if (buf_append(&c->rx, chunk, (size_t)n) != 0) {
            conn_fail(c, -ENOMEM);
        }
        return 1;
    }
    if (n == 0) {
        deliver_rx(c);
        conn_fail(c, 0);
        return 0;
    }
    if (errno == EINTR) {
        return 1;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        conn_fail(c, -errno);
    }
    return 0;
}

static void conn_read(tcp_conn_t *c)
{
    uint8_t chunk[TCP_READ_CHUNK];

    while (!c->closing && !c->finalize_scheduled) {
        size_t room = TCP_RX_MAX - buf_readable(&c->rx);

        if (room == 0) {
            deliver_rx(c);
            if (c->closing || c->finalize_scheduled) {
                return;
            }
            if (buf_readable(&c->rx) == TCP_RX_MAX) {
                conn_fail(c, -ENOBUFS);
                return;
            }
            continue;
        }
        if (!read_chunk_into_rx(c, chunk, room < sizeof chunk ? room : sizeof chunk)) {
            break;
        }
    }
    deliver_rx(c);
}

static void flush_tx(tcp_conn_t *c)
{
    while (!c->finalize_scheduled && !c->connecting && buf_readable(&c->tx) > 0) {
        ssize_t n = send(c->fd, buf_peek(&c->tx), buf_readable(&c->tx), MSG_NOSIGNAL);

        if (n > 0) {
            buf_consume(&c->tx, (size_t)n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            conn_fail(c, n < 0 ? -errno : -EIO);
            return;
        }
    }
    if (c->finalize_scheduled || c->connecting) {
        return;
    }
    c->want_write = buf_readable(&c->tx) > 0;
    if (!c->want_write && c->closing && c->graceful) {
        shutdown(c->fd, SHUT_WR);
        schedule_finalize(c);
        return;
    }
    sync_events(c);
}

static void on_connect_ready(tcp_conn_t *c)
{
    int err = 0;
    socklen_t len = sizeof err;

    if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        conn_fail(c, -errno);
        return;
    }
    if (err != 0) {
        conn_fail(c, -err);
        return;
    }
    c->connecting = 0;
    sync_events(c);
    flush_tx(c);
    if (c->finalize_scheduled || c->closing) {
        return;
    }
    if (c->has_handlers && c->handlers.on_connect != NULL) {
        c->handlers.on_connect(c, c->ud);
    }
}

static void tcp_conn_on_io(loop_t *l, int fd, uint32_t events, void *ud)
{
    tcp_conn_t *c = (tcp_conn_t *)ud;

    (void)l;
    (void)fd;
    if (c->finalize_scheduled) {
        return;
    }
    if (c->connecting) {
        on_connect_ready(c);
        return;
    }
    if (events & LOOP_WRITE) {
        flush_tx(c);
    }
    if (!c->finalize_scheduled && (events & (LOOP_READ | LOOP_ERROR))) {
        conn_read(c);
    }
}

tcp_conn_t *tcp_conn_new(loop_t *l, int fd, int connecting)
{
    tcp_conn_t *c = calloc(1, sizeof *c);

    if (c == NULL) {
        return NULL;
    }
    c->loop = l;
    c->fd = fd;
    c->connecting = connecting;
    if (buf_init(&c->rx, TCP_RX_MAX) != 0 || buf_init(&c->tx, TCP_TX_MAX) != 0) {
        buf_free(&c->rx);
        free(c);
        return NULL;
    }
    c->events = events_for(c);
    if (loop_add_fd(l, fd, c->events, tcp_conn_on_io, c) != 0) {
        buf_free(&c->rx);
        buf_free(&c->tx);
        free(c);
        return NULL;
    }
    return c;
}

tcp_conn_t *tcp_connect(loop_t *l, const char *host, uint16_t port, const tcp_handlers *h, void *ud)
{
    struct sockaddr_storage sa;
    socklen_t len = 0;
    tcp_conn_t *c;
    int fd;

    if (l == NULL || tcp_resolve(host, port, &sa, &len) != 0) {
        errno = EINVAL;
        return NULL;
    }
    fd = socket(sa.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return NULL;
    }
    if (connect(fd, (const struct sockaddr *)&sa, len) != 0 && errno != EINPROGRESS) {
        int err = errno;
        close(fd);
        errno = err;
        return NULL;
    }
    c = tcp_conn_new(l, fd, 1);
    if (c == NULL) {
        close(fd);
        return NULL;
    }
    tcp_set_handlers(c, h, ud);
    return c;
}

void tcp_set_handlers(tcp_conn_t *c, const tcp_handlers *h, void *ud)
{
    if (c == NULL) {
        return;
    }
    c->ud = ud;
    c->has_handlers = h != NULL;
    if (h != NULL) {
        c->handlers = *h;
    }
}

int tcp_send(tcp_conn_t *c, const void *data, size_t n)
{
    if (c == NULL || c->closing || c->finalize_scheduled) {
        return -EPIPE;
    }
    if (buf_readable(&c->tx) + n > TCP_TX_MAX) {
        return TCP_ETXFULL;
    }
    if (n == 0) {
        return 0;
    }
    if (buf_append(&c->tx, data, n) != 0) {
        return -ENOMEM;
    }
    flush_tx(c);
    return 0;
}

void tcp_close(tcp_conn_t *c, int graceful)
{
    if (c == NULL || c->closing) {
        return;
    }
    c->closing = 1;
    c->graceful = graceful;
    if (!graceful || c->connecting || c->finalize_scheduled) {
        schedule_finalize(c);
        return;
    }
    flush_tx(c);
}

const char *tcp_peer_addr(const tcp_conn_t *c, char *out, size_t n)
{
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    char ip[INET6_ADDRSTRLEN];

    if (c == NULL || out == NULL || n == 0 || getpeername(c->fd, (struct sockaddr *)&ss, &len) != 0) {
        return NULL;
    }
    if (ss.ss_family == AF_INET6) {
        const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)&ss;
        inet_ntop(AF_INET6, &s6->sin6_addr, ip, sizeof ip);
        snprintf(out, n, "[%s]:%u", ip, (unsigned)ntohs(s6->sin6_port));
    } else {
        const struct sockaddr_in *s4 = (const struct sockaddr_in *)&ss;
        inet_ntop(AF_INET, &s4->sin_addr, ip, sizeof ip);
        snprintf(out, n, "%s:%u", ip, (unsigned)ntohs(s4->sin_port));
    }
    return out;
}

int tcp_set_nodelay(tcp_conn_t *c, int on)
{
    int v = on ? 1 : 0;

    if (c == NULL) {
        return -EINVAL;
    }
    return setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof v) == 0 ? 0 : -errno;
}
