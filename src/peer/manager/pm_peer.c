/* peer/manager — table des pairs : états, double session (clé initiatrice la plus petite gagne),
 * connexion sortante, backoff exponentiel plafonné, anneau de dédup. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "core/log/log.h"
#include "peer/manager/pm_internal.h"

#define PM_MOD "peer/manager"
#define PM_BACKOFF_BASE_MS 1000u
#define PM_BACKOFF_MAX_MS  30000u

static const tcp_handlers PM_NO_HANDLERS = { NULL, NULL, NULL };

pm_peer_t *pm_peer_find(pm *pm, const uint8_t pub[32])
{
    pm_peer_t *p = pm->peers;

    while (p && memcmp(p->pub, pub, 32) != 0) {
        p = p->next;
    }
    return p;
}

pm_peer_t *pm_peer_get_or_add(pm *pm, const uint8_t pub[32])
{
    pm_peer_t *p = pm_peer_find(pm, pub);

    if (p) {
        return p;
    }
    p = calloc(1, sizeof *p);
    if (!p) {
        LOG_E(PM_MOD, "allocation pair échouée");
        return NULL;
    }
    memcpy(p->pub, pub, 32);
    p->state = PM_OFFLINE;
    p->backoff_ms = pm_scale(pm, PM_BACKOFF_BASE_MS);
    p->next = pm->peers;
    pm->peers = p;
    pm->n_peers++;
    return p;
}

pm_state pm_peer_compute(const pm_peer_t *p)
{
    if (p->cur && p->cur->hello_ok) {
        return PM_CONNECTED;
    }
    if (p->cur || p->dial) {
        return PM_CONNECTING;
    }
    return PM_OFFLINE;
}

void pm_peer_set_state(pm *pm, pm_peer_t *p)
{
    pm_state st = pm_peer_compute(p);

    if (pm->closing || st == p->state) {
        return;
    }
    p->state = st;
    if (pm->ev.on_peer) {
        pm->ev.on_peer(p->pub, p->name, st, pm->ud);
    }
}

/* Initiateur effectif d'une session : nous si sortante, le pair sinon. */
static const uint8_t *initiator_key(const pm *pm, const pm_peer_t *p, const pm_link_t *l)
{
    return l->outbound ? pm->identity->pub : p->pub;
}

int pm_peer_resolve(pm *pm, pm_peer_t *p, pm_link_t *nl)
{
    pm_link_t *cur = p->cur;

    if (!cur || cur == nl) {
        p->cur = nl;
        return 0;
    }
    if (memcmp(initiator_key(pm, p, nl), initiator_key(pm, p, cur), 32) < 0) {
        LOG_I(PM_MOD, "double session : la session de la clé la plus petite est gardée");
        p->cur = nl;
        sess_close(cur->sess, 0);
        return 0;
    }
    LOG_I(PM_MOD, "double session : nouvelle session refusée");
    return -1;
}

void pm_peer_schedule_retry(pm *pm, pm_peer_t *p, uint64_t now)
{
    uint64_t cap = pm_scale(pm, PM_BACKOFF_MAX_MS);

    p->next_try_ms = now + p->backoff_ms;
    p->backoff_ms = p->backoff_ms * 2 > cap ? cap : p->backoff_ms * 2;
}

void pm_peer_backoff_reset(pm *pm, pm_peer_t *p)
{
    p->backoff_ms = pm_scale(pm, PM_BACKOFF_BASE_MS);
    p->next_try_ms = 0;
}

void pm_peer_link_closed(pm *pm, pm_link_t *l)
{
    pm_peer_t *p = l->peer;
    int was_active;

    if (!p) {
        return;
    }
    was_active = (p->dial == l) || (p->cur == l);
    if (p->dial == l) {
        p->dial = NULL;
    }
    if (p->cur == l) {
        p->cur = NULL;
    }
    if (was_active && p->cur == NULL && p->dial == NULL) {
        pm_peer_schedule_retry(pm, p, loop_now_ms(pm->loop));
    }
    pm_peer_set_state(pm, p);
}

void pm_peer_dial(pm *pm, pm_peer_t *p, uint64_t now)
{
    tcp_conn_t *c;
    pm_link_t *l;
    sess_handlers h = pm_sess_handlers();

    if (!p->has_addr || p->cur || p->dial || now < p->next_try_ms) {
        return;
    }
    c = tcp_connect(pm->loop, p->host, p->port, &PM_NO_HANDLERS, NULL);
    if (!c) {
        LOG_W(PM_MOD, "connexion sortante impossible");
        pm_peer_schedule_retry(pm, p, now);
        return;
    }
    l = pm_link_new(pm, 1);
    if (!l) {
        tcp_close(c, 0);
        return;
    }
    l->peer = p;
    pm_conn_tune(c);
    pm_link_attach(pm, l, sess_new(pm->loop, c, 0, SESS_INITIATOR, pm->identity, &h, l));
    if (!l->sess) {
        pm_link_free(l);
        pm_peer_schedule_retry(pm, p, now);
        return;
    }
    p->dial = l;
    pm_peer_set_state(pm, p);
}

int pm_peer_ring_seen(pm_peer_t *p, uint64_t id)
{
    size_t i;

    for (i = 0; i < p->ring_n; i++) {
        if (p->ring[i] == id) {
            return 1;
        }
    }
    return 0;
}

void pm_peer_ring_add(pm_peer_t *p, uint64_t id)
{
    p->ring[p->ring_pos] = id;
    p->ring_pos = (p->ring_pos + 1u) % PM_DEDUP_RING;
    if (p->ring_n < PM_DEDUP_RING) {
        p->ring_n++;
    }
}

void pm_peer_free_all(pm *pm)
{
    pm_peer_t *p = pm->peers;
    pm_peer_t *next;

    while (p) {
        next = p->next;
        pm_queue_drop_all(pm, p);
        free(p);
        p = next;
    }
    pm->peers = NULL;
    pm->n_peers = 0;
}
