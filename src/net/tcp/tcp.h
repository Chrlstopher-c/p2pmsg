/* net/tcp — connexions TCP non bloquantes sur core/loop, tampons d'entrée/sortie bornés. */
#ifndef P2P_NET_TCP_H
#define P2P_NET_TCP_H
#include <stddef.h>
#include <stdint.h>
#include "core/loop/loop.h"
#include "core/buf/buf.h"

typedef struct tcp_conn tcp_conn_t;
typedef struct tcp_listener tcp_listener_t;

#define TCP_RX_MAX (256u * 1024u)
#define TCP_TX_MAX (1024u * 1024u)
#define TCP_ETXFULL (-1101)

typedef struct {
    void (*on_connect)(tcp_conn_t *c, void *ud);          /* connexion sortante établie */
    void (*on_data)(tcp_conn_t *c, buf_t *rx, void *ud);  /* rx = tampon d'entrée ; consommer ce qui est traité */
    void (*on_close)(tcp_conn_t *c, int err, void *ud);   /* err 0 = fermeture propre, sinon -errno ;
                                                             c est libéré au retour, ne plus l'utiliser */
} tcp_handlers;

/* Poser les handlers via tcp_set_handlers. */
typedef void (*tcp_accept_cb)(tcp_listener_t *ls, tcp_conn_t *c, void *ud);

/* host = IPv4/IPv6 littéral ("0.0.0.0", "::"). port 0 = éphémère. SO_REUSEADDR. NULL si échec (errno). */
tcp_listener_t *tcp_listen(loop_t *l, const char *host, uint16_t port, tcp_accept_cb cb, void *ud);
uint16_t tcp_listener_port(const tcp_listener_t *ls);
void     tcp_listener_close(tcp_listener_t *ls);

/* Connexion asynchrone ; on_connect ou on_close(err) plus tard, jamais en synchrone. NULL si échec immédiat. */
tcp_conn_t *tcp_connect(loop_t *l, const char *host, uint16_t port, const tcp_handlers *h, void *ud);
void tcp_set_handlers(tcp_conn_t *c, const tcp_handlers *h, void *ud);

/* Copie dans le tampon de sortie. 0 ou TCP_ETXFULL (rien copié). */
int  tcp_send(tcp_conn_t *c, const void *data, size_t n);
/* Fermeture : graceful=1 vide le tampon de sortie puis shutdown ; 0 = immédiat. on_close(0) appelé
 * ensuite de façon asynchrone (prochaine itération), jamais pendant l'appel. Idempotent. */
void tcp_close(tcp_conn_t *c, int graceful);

/* "ip:port" du pair, dans out (>= 64 octets). */
const char *tcp_peer_addr(const tcp_conn_t *c, char *out, size_t n);
int tcp_set_nodelay(tcp_conn_t *c, int on);

#endif
