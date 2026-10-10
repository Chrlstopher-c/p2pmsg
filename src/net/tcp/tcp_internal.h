/* net/tcp, privé : structures partagées entre tcp_listen.c et tcp_conn.c. Ne pas inclure hors du module. */
#ifndef P2P_NET_TCP_INTERNAL_H
#define P2P_NET_TCP_INTERNAL_H
#include <netinet/in.h>
#include <sys/socket.h>
#include "net/tcp/tcp.h"

struct tcp_listener {
    loop_t *loop;
    int fd;
    uint16_t port;
    tcp_accept_cb cb;
    void *ud;
};

struct tcp_conn {
    loop_t *loop;
    int fd;
    uint32_t events;
    int connecting;
    int want_write;
    int closing;
    int graceful;
    int finalize_scheduled;
    int err;
    int has_handlers;
    tcp_handlers handlers;
    void *ud;
    buf_t rx;
    buf_t tx;
};

/* Résolution littérale IPv4/IPv6 (AI_NUMERICHOST) → sockaddr. 0 ou -EINVAL. */
int tcp_resolve(const char *host, uint16_t port, struct sockaddr_storage *out, socklen_t *len);

/* Conn sur fd non bloquant ; connecting=1 pour une sortie en cours. NULL si échec, fd à la charge de l'appelant. */
tcp_conn_t *tcp_conn_new(loop_t *l, int fd, int connecting);

#endif
