/* net/discovery — table des pairs connus : dedoublonnage, filtre de sa propre pk, emission on_peer / on_lost. */
#include <string.h>
#include "core/log/log.h"
#include "net/discovery/disc_internal.h"

static int find_pk(const disc_t *d, const uint8_t pk[32])
{
    int i;

    for (i = 0; i < d->n_known; i++) {
        if (memcmp(d->known[i].peer.pubkey, pk, 32) == 0) {
            return i;
        }
    }
    return -1;
}

static int same_endpoint(const disc_peer *a, const disc_peer *b)
{
    return a->port == b->port && strcmp(a->host, b->host) == 0 && strcmp(a->name, b->name) == 0;
}

static void set_svc(disc_known *k, const char *svc)
{
    size_t n = strlen(svc);

    if (n > DISC_NAME_MAX) {
        n = DISC_NAME_MAX;
    }
    memcpy(k->svc, svc, n);
    k->svc[n] = '\0';
}

static int add_known(disc_t *d, const disc_peer *p)
{
    if (d->n_known >= DISC_KNOWN_MAX) {
        LOG_W(DISC_LOG, "peer table full, peer ignored host=%s", p->host);
        return -1;
    }
    d->known[d->n_known].peer = *p;
    d->known[d->n_known].svc[0] = '\0';
    return d->n_known++;
}

/* Emet on_peer seulement si le pair est nouveau ou si son adresse / nom a change.
 * Le pointeur passe a on_peer n'est valide que pendant le callback. */
void disc_peers_seen(disc_t *d, const disc_peer *p, const char *svc)
{
    int idx;

    if (memcmp(p->pubkey, d->pk, sizeof d->pk) == 0) {
        return;
    }
    idx = find_pk(d, p->pubkey);
    if (idx >= 0) {
        if (svc) {
            set_svc(&d->known[idx], svc);
        }
        if (same_endpoint(&d->known[idx].peer, p)) {
            return;
        }
        d->known[idx].peer = *p;
    } else {
        idx = add_known(d, p);
        if (idx < 0) {
            return;
        }
        if (svc) {
            set_svc(&d->known[idx], svc);
        }
    }
    d->h.on_peer(&d->known[idx].peer, d->ud);
}

/* Retire le pair associe au nom de service mDNS et emet on_lost avec sa pk. */
void disc_peers_lost(disc_t *d, const char *svc)
{
    uint8_t pk[32];
    int i;

    if (!svc) {
        return;
    }
    for (i = 0; i < d->n_known; i++) {
        if (d->known[i].svc[0] != '\0' && strcmp(d->known[i].svc, svc) == 0) {
            break;
        }
    }
    if (i == d->n_known) {
        return;
    }
    memcpy(pk, d->known[i].peer.pubkey, sizeof pk);
    memmove(&d->known[i], &d->known[i + 1], (size_t)(d->n_known - i - 1) * sizeof d->known[0]);
    d->n_known--;
    if (d->h.on_lost) {
        d->h.on_lost(pk, d->ud);
    }
}
