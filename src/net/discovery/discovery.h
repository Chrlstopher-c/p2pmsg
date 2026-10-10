/* net/discovery — pairs depuis (1) fichier statique, (2) mDNS Avahi `_p2pmsg._tcp`, intégré à core/loop
 * via un AvahiPoll maison (pas de thread). TXT : "name=<nom>", "pk=<hex 64>", "v=1".
 * Fichier statique : une entrée par ligne `host:port pubkey_hex [nom]`, `#` = commentaire, IPv6 en `[addr]:port`.
 * Ligne invalide → ignorée avec LOG_W (numéro de ligne), jamais fatale. */
#ifndef P2P_NET_DISCOVERY_H
#define P2P_NET_DISCOVERY_H
#include <stdint.h>
#include "core/loop/loop.h"

#define DISC_NAME_MAX 64u
#define DISC_HOST_MAX 64u

typedef enum { DISC_STATIC, DISC_MDNS } disc_source;

typedef struct {
    uint8_t     pubkey[32];
    char        name[DISC_NAME_MAX + 1];
    char        host[DISC_HOST_MAX];   /* littéral IP */
    uint16_t    port;
    disc_source source;
} disc_peer;

typedef struct {
    const char *name;           /* nom d'affichage annoncé */
    const uint8_t *pubkey;      /* 32 octets, annoncé */
    uint16_t    port;           /* port d'écoute annoncé */
    const char *peers_file;     /* NULL = pas de liste statique */
    int         enable_mdns;
} disc_config;

typedef struct {
    void (*on_peer)(const disc_peer *p, void *ud);    /* nouveau pair ou adresse/nom changé ; jamais soi-même */
    void (*on_lost)(const uint8_t pubkey[32], void *ud); /* mDNS seulement (retrait du service) */
} disc_handlers;

typedef struct disc disc_t;

/* Les pairs statiques sont émis via on_peer au 1er tour de boucle (pas en synchrone). Avahi absent/indisponible :
 * LOG_W et continue en statique seul. NULL si config invalide. */
disc_t *disc_new(loop_t *l, const disc_config *cfg, const disc_handlers *h, void *ud);
void    disc_free(disc_t *d);

/* Exposé pour tests. Retourne le nombre d'entrées valides écrites (≤ max) ou -errno si fichier illisible. */
int disc_parse_peers_file(const char *path, disc_peer *out, int max);

#endif
