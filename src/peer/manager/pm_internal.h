/* peer/manager — structures internes partagées entre pm.c, pm_peer.c, pm_queue.c, pm_wire.c. */
#ifndef P2P_PEER_MANAGER_INTERNAL_H
#define P2P_PEER_MANAGER_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
#include "peer/manager/manager.h"
#include "core/buf/buf.h"
#include "net/tcp/tcp.h"
#include "net/discovery/discovery.h"
#include "proto/msg/msg.h"

#define PM_DEDUP_RING 1024u
#define MSG_NAME_MAX_PM (MSG_NAME_MAX + 1u)
#define DISC_HOST_MAX_PM DISC_HOST_MAX

typedef struct pm pm;
typedef struct pm_entry pm_entry_t;
typedef struct pm_link pm_link_t;
typedef struct pm_peer pm_peer_t;

struct pm_entry {
    uint64_t    id;
    uint64_t    ts_ms;
    uint16_t    text_len;
    char       *text;
    uint32_t    sends;
    uint64_t    last_sent_ms;
    pm_entry_t *next;
};

struct pm_peer {
    uint8_t     pub[32];
    char        name[MSG_NAME_MAX_PM];
    char        host[DISC_HOST_MAX_PM];
    uint16_t    port;
    int         has_addr;
    pm_state    state;
    uint64_t    next_try_ms;
    uint64_t    backoff_ms;
    pm_link_t  *cur;       /* session vérifiée retenue (unique) */
    pm_link_t  *dial;      /* session sortante en cours (non fermée) */
    pm_entry_t *q_head;
    pm_entry_t *q_tail;
    size_t      q_len;
    uint64_t    ring[PM_DEDUP_RING];
    size_t      ring_n;
    size_t      ring_pos;
    pm_peer_t  *next;
};

struct pm_link {
    pm_t       *pm;          /* NULL après pm_free : on_close ne touche plus au manager */
    pm_peer_t  *peer;        /* NULL tant que non vérifiée (entrant) */
    sess_t     *sess;
    int         outbound;
    int         hello_ok;
    uint64_t    last_rx_ms;
    uint64_t    last_tx_ms;
    pm_link_t  *lnext;       /* liste des liens vivants du manager */
    pm_link_t  *lprev;
};

struct pm {
    loop_t          *loop;
    const sess_identity *identity;
    char             name[MSG_NAME_MAX_PM];
    pm_events        ev;
    void            *ud;
    uint32_t         div;
    tcp_listener_t  *listener;
    disc_t          *disc;
    disc_config      dcfg;
    uint64_t         tick_id;
    pm_peer_t       *peers;
    size_t           n_peers;
    size_t           q_total;
    pm_link_t       *links;
    int              closing;
};

#define PM_DRAIN_MS 500u /* borne de la pompe de fermeture dans pm_free */

/* Désactive Nagle sur une connexion (latence) ; échec journalisé. */
void pm_conn_tune(tcp_conn_t *c);

/* Durées réelles divisées par time_div (min 1 ms). */
uint64_t pm_scale(const pm *pm, uint64_t ms);

/* pm_peer.c */
pm_peer_t *pm_peer_find(pm *pm, const uint8_t pub[32]);
pm_peer_t *pm_peer_get_or_add(pm *pm, const uint8_t pub[32]);
void       pm_peer_set_state(pm *pm, pm_peer_t *p);
pm_state   pm_peer_compute(const pm_peer_t *p);
int        pm_peer_resolve(pm *pm, pm_peer_t *p, pm_link_t *nl);
void       pm_peer_link_closed(pm *pm, pm_link_t *l);
void       pm_peer_dial(pm *pm, pm_peer_t *p, uint64_t now);
void       pm_peer_backoff_reset(pm *pm, pm_peer_t *p);
void       pm_peer_schedule_retry(pm *pm, pm_peer_t *p, uint64_t now);
int        pm_peer_ring_seen(pm_peer_t *p, uint64_t id);
void       pm_peer_ring_add(pm_peer_t *p, uint64_t id);
void       pm_peer_free_all(pm *pm);

/* pm_queue.c */
int  pm_queue_push(pm *pm, pm_peer_t *p, uint64_t id, uint64_t ts_ms, const char *text);
void pm_queue_ack(pm *pm, pm_peer_t *p, uint64_t id);
void pm_queue_pump(pm *pm, pm_link_t *l, uint64_t now);
void pm_queue_drop_all(pm *pm, pm_peer_t *p);

/* pm_wire.c */
int  pm_wire_send(pm_link_t *l, const msg_t *m, uint64_t now);
void pm_wire_on_frame(pm_link_t *l, uint8_t type, const uint8_t *pl, size_t len);
void pm_wire_ping_check(pm *pm, pm_link_t *l, uint64_t now);
void pm_wire_hello(pm_link_t *l, const pm *pm, uint64_t now);

/* Lien entrant/sortant : création et handlers de session. */
pm_link_t *pm_link_new(pm *pm, int outbound);
void        pm_link_free(pm_link_t *l);
void        pm_link_attach(pm *pm, pm_link_t *l, sess_t *s);
sess_handlers pm_sess_handlers(void);

#endif
