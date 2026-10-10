#define _GNU_SOURCE
/* net/tcp : écoute, acceptation en boucle et résolution d'adresses littérales. */
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "core/log/log.h"
#include "net/tcp/tcp_internal.h"

#define TCP_MOD "net/tcp"
#define TCP_BACKLOG 128

int tcp_resolve(const char *host, uint16_t port, struct sockaddr_storage *out, socklen_t *len)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    char port_str[8];
    int rc;

    if (host == NULL) {
        return -EINVAL;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    snprintf(port_str, sizeof port_str, "%u", (unsigned)port);
    rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0 || res == NULL) {
        LOG_W(TCP_MOD, "resolve échoué host=%s err=%s", host, gai_strerror(rc));
        return -EINVAL;
    }
    memcpy(out, res->ai_addr, res->ai_addrlen);
    *len = res->ai_addrlen;
    freeaddrinfo(res);
    return 0;
}

static int listener_socket(const struct sockaddr_storage *sa, socklen_t len)
{
    int fd = socket(sa->ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    int one = 1;

    if (fd < 0) {
        return -errno;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) != 0
        || bind(fd, (const struct sockaddr *)sa, len) != 0
        || listen(fd, TCP_BACKLOG) != 0) {
        int err = errno;
        close(fd);
        return -err;
    }
    return fd;
}

static uint16_t listener_bound_port(int fd)
{
    struct sockaddr_storage ss;
    socklen_t slen = sizeof ss;

    if (getsockname(fd, (struct sockaddr *)&ss, &slen) != 0) {
        return 0;
    }
    if (ss.ss_family == AF_INET6) {
        return ntohs(((struct sockaddr_in6 *)&ss)->sin6_port);
    }
    return ntohs(((struct sockaddr_in *)&ss)->sin_port);
}

static void accept_all(tcp_listener_t *ls)
{
    for (;;) {
        int fd = accept4(ls->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        tcp_conn_t *c;

        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_W(TCP_MOD, "accept échoué err=%d", errno);
            }
            return;
        }
        c = tcp_conn_new(ls->loop, fd, 0);
        if (c == NULL) {
            close(fd);
            continue;
        }
        if (ls->cb != NULL) {
            ls->cb(ls, c, ls->ud);
        }
    }
}

static void listener_on_io(loop_t *l, int fd, uint32_t events, void *ud)
{
    tcp_listener_t *ls = (tcp_listener_t *)ud;

    (void)l;
    (void)fd;
    if (events & LOOP_ERROR) {
        LOG_W(TCP_MOD, "erreur sur listener port=%u", (unsigned)ls->port);
    }
    accept_all(ls);
}

tcp_listener_t *tcp_listen(loop_t *l, const char *host, uint16_t port, tcp_accept_cb cb, void *ud)
{
    struct sockaddr_storage sa;
    socklen_t len = 0;
    tcp_listener_t *ls;
    int fd;

    if (l == NULL || tcp_resolve(host, port, &sa, &len) != 0) {
        errno = EINVAL;
        return NULL;
    }
    fd = listener_socket(&sa, len);
    if (fd < 0) {
        errno = -fd;
        return NULL;
    }
    ls = calloc(1, sizeof *ls);
    if (ls == NULL) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    ls->loop = l;
    ls->fd = fd;
    ls->cb = cb;
    ls->ud = ud;
    ls->port = listener_bound_port(fd);
    if (loop_add_fd(l, fd, LOOP_READ, listener_on_io, ls) != 0) {
        close(fd);
        free(ls);
        errno = EIO;
        return NULL;
    }
    return ls;
}

uint16_t tcp_listener_port(const tcp_listener_t *ls)
{
    return ls == NULL ? 0 : ls->port;
}

void tcp_listener_close(tcp_listener_t *ls)
{
    if (ls == NULL) {
        return;
    }
    loop_del_fd(ls->loop, ls->fd);
    close(ls->fd);
    free(ls);
}
