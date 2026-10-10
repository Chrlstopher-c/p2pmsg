/* peer/manager — liens (une session sécurisée par lien) : vérification de clé (F4), cycle de vie, handlers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/log/log.h"
#include "peer/manager/pm_internal.h"

#define PM_MOD "peer/manager"

static void fingerprint(const uint8_t pub[32], char out[9])
{
    snprintf(out, 9, "%02x%02x%02x%02x", pub[0], pub[1], pub[2], pub[3]);
}

pm_link_t *pm_link_new(pm *pm, int outbound)
{
    pm_link_t *l = calloc(1, sizeof *l);

    if (!l) {
        LOG_E(PM_MOD, "allocation lien échouée");
        return NULL;
    }
    l->pm = pm;
    l->outbound = outbound;
    l->last_rx_ms = loop_now_ms(pm->loop);
    l->last_tx_ms = l->last_rx_ms;
    l->lnext = pm->links;
    if (pm->links) {
        pm->links->lprev = l;
    }
    pm->links = l;
    return l;
}

void pm_link_free(pm_link_t *l)
{
    if (l->pm) {
        if (l->lprev) {
            l->lprev->lnext = l->lnext;
        } else {
            l->pm->links = l->lnext;
        }
        if (l->lnext) {
            l->lnext->lprev = l->lprev;
        }
    }
    free(l);
}

void pm_link_attach(pm *pm, pm_link_t *l, sess_t *s)
{
    (void)pm;
    l->sess = s;
}

static int link_verify(sess_t *s, const uint8_t peer_pub[32], void *ud)
{
    pm_link_t *l = ud;
    pm *pm = l->pm;
    pm_peer_t *p;
    char fp[9];

    (void)s;
    if (!pm) {
        return -1;
    }
    if (l->outbound) {
        p = l->peer;
        if (memcmp(p->pub, peer_pub, 32) != 0) {
            fingerprint(peer_pub, fp);
            LOG_W(PM_MOD, "clé inattendue pour l'adresse, rejet fp=%s", fp);
            return -1;
        }
    } else {
        p = pm_peer_find(pm, peer_pub);
        if (!p) {
            fingerprint(peer_pub, fp);
            LOG_W(PM_MOD, "entrant de clé inconnue, rejet fp=%s", fp);
            return -1;
        }
        l->peer = p;
    }
    return pm_peer_resolve(pm, p, l);
}

static void link_established(sess_t *s, void *ud)
{
    pm_link_t *l = ud;

    (void)s;
    if (l->pm) {
        pm_wire_hello(l, l->pm, loop_now_ms(l->pm->loop));
    }
}

static void link_message(sess_t *s, uint8_t type, const uint8_t *payload, size_t len, void *ud)
{
    pm_link_t *l = ud;

    (void)s;
    pm_wire_on_frame(l, type, payload, len);
}

static void link_close(sess_t *s, int err, void *ud)
{
    pm_link_t *l = ud;

    (void)s;
    (void)err;
    if (l->pm) {
        pm_peer_link_closed(l->pm, l);
    }
    pm_link_free(l);
}

sess_handlers pm_sess_handlers(void)
{
    sess_handlers h;

    memset(&h, 0, sizeof h);
    h.on_verify = link_verify;
    h.on_established = link_established;
    h.on_message = link_message;
    h.on_close = link_close;
    return h;
}

void pm_conn_tune(tcp_conn_t *c)
{
    if (c && tcp_set_nodelay(c, 1) != 0) {
        LOG_W(PM_MOD, "TCP_NODELAY refusé, latence possiblement accrue");
    }
}
