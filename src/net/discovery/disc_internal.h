/* net/discovery — structures et fonctions partagées entre les fichiers privés du module. */
#ifndef P2P_NET_DISCOVERY_INTERNAL_H
#define P2P_NET_DISCOVERY_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
#include <avahi-common/watch.h>
#include "core/loop/loop.h"
#include "net/discovery/discovery.h"

#define DISC_LOG "disc"
#define DISC_STATIC_MAX 64
#define DISC_KNOWN_MAX 64
#define DISC_SVC_TYPE "_p2pmsg._tcp"
#define DISC_TXT_VERSION "1"

typedef struct disc_mdns disc_mdns_t;

typedef struct {
    disc_peer peer;
    char      svc[DISC_NAME_MAX + 1]; /* nom de service mDNS ; vide pour un pair statique */
} disc_known;

struct disc {
    loop_t      *loop;
    disc_handlers h;
    void        *ud;
    uint8_t      pk[32];
    char         name[DISC_NAME_MAX + 1];
    uint16_t     port;
    disc_peer    static_peers[DISC_STATIC_MAX];
    int          n_static;
    uint64_t     first_timer;
    disc_known   known[DISC_KNOWN_MAX];
    int          n_known;
    disc_mdns_t *mdns;
};

/* disc_static.c : 0 si s est exactement 64 caracteres hex, sinon -EINVAL. */
int disc_parse_hex_key(const char *s, size_t len, uint8_t out[32]);

/* disc_peers.c : dedoublonnage, filtre de sa propre pk, emission on_peer / on_lost. */
void disc_peers_seen(disc_t *d, const disc_peer *p, const char *svc);
void disc_peers_lost(disc_t *d, const char *svc);

/* disc_avahi_poll.c : AvahiPoll branche sur core/loop. */
void disc_poll_init(AvahiPoll *api, loop_t *l);

/* disc_mdns.c : NULL seulement si allocation impossible. Avahi absent = objet valide, reessai programme. */
disc_mdns_t *disc_mdns_new(disc_t *d);
void         disc_mdns_free(disc_mdns_t *m);

#endif
