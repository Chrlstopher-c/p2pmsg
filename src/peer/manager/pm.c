/* peer/manager — cycle de vie, écoute, découverte, tick périodique et API publique. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/log/log.h"
#include "peer/manager/pm_internal.h"

#define PM_MOD "peer/manager"
#define PM_TICK_MS 50u

uint64_t pm_scale(const pm *pm, uint64_t ms)
{
    uint64_t v = ms / pm->div;

    return v ? v : 1u;
}

static void pm_on_accept(tcp_listener_t *ls, tcp_conn_t *c, void *ud)
{
    pm *pm = ud;
    pm_link_t *l;
    sess_handlers h = pm_sess_handlers();

    (void)ls;
    l = pm_link_new(pm, 0);
    if (!l) {
        tcp_close(c, 0);
        return;
    }
    pm_conn_tune(c);
    pm_link_attach(pm, l, sess_new(pm->loop, c, 1, SESS_RESPONDER, pm->identity, &h, l));
    if (!l->sess) {
        pm_link_free(l);
    }
}

static void disc_on_peer(const disc_peer *dp, void *ud)
{
    pm *pm = ud;
    pm_peer_t *p = pm_peer_get_or_add(pm, dp->pubkey);

    if (!p) {
        return;
    }
    snprintf(p->name, sizeof p->name, "%s", dp->name);
    snprintf(p->host, sizeof p->host, "%s", dp->host);
    p->port = dp->port;
    p->has_addr = 1;
}

static void disc_on_lost(const uint8_t pubkey[32], void *ud)
{
    pm *pm = ud;

    (void)pubkey;
    LOG_D(PM_MOD, "pair retiré de mDNS, adresse conservée");
    (void)pm;
}

static void tick_peers(pm *pm, uint64_t now)
{
    pm_peer_t *p;

    for (p = pm->peers; p; p = p->next) {
        pm_peer_dial(pm, p, now);
    }
}

static void tick_links(pm *pm, uint64_t now)
{
    pm_link_t *l;

    for (l = pm->links; l; l = l->lnext) {
        pm_wire_ping_check(pm, l, now);
        if (l->peer && l->peer->cur == l) {
            pm_queue_pump(pm, l, now);
        }
    }
}

static void pm_tick(loop_t *loop, uint64_t id, void *ud)
{
    pm *pm = ud;
    uint64_t now = loop_now_ms(loop);

    (void)id;
    tick_peers(pm, now);
    tick_links(pm, now);
}

static void pm_destroy_partial(pm *pm)
{
    if (pm->disc) {
        disc_free(pm->disc);
    }
    if (pm->listener) {
        tcp_listener_close(pm->listener);
    }
    free(pm);
}

pm_t *pm_new(loop_t *l, const pm_config *cfg, const pm_events *ev, void *ud)
{
    pm *pm;
    disc_handlers dh = { disc_on_peer, disc_on_lost };

    if (!l || !cfg || !cfg->identity || !ev) {
        return NULL;
    }
    pm = calloc(1, sizeof *pm);
    if (!pm) {
        return NULL;
    }
    pm->loop = l;
    pm->identity = cfg->identity;
    pm->ev = *ev;
    pm->ud = ud;
    pm->div = cfg->time_div ? cfg->time_div : 1u;
    snprintf(pm->name, sizeof pm->name, "%s", cfg->name ? cfg->name : "");
    pm->listener = tcp_listen(l, cfg->listen_host ? cfg->listen_host : "0.0.0.0", cfg->listen_port,
                              pm_on_accept, pm);
    if (!pm->listener) {
        LOG_E(PM_MOD, "écoute TCP impossible");
        free(pm);
        return NULL;
    }
    pm->dcfg = cfg->disc;
    pm->dcfg.name = pm->name;
    pm->dcfg.pubkey = pm->identity->pub;
    pm->dcfg.port = tcp_listener_port(pm->listener);
    pm->disc = disc_new(l, &pm->dcfg, &dh, pm);
    pm->tick_id = loop_timer_add(l, pm_scale(pm, PM_TICK_MS), pm_scale(pm, PM_TICK_MS), pm_tick, pm);
    if (!pm->disc || pm->tick_id == 0) {
        LOG_E(PM_MOD, "initialisation découverte/tick impossible");
        pm_destroy_partial(pm);
        return NULL;
    }
    return pm;
}

static void link_say_bye(pm_link_t *l, uint64_t now)
{
    msg_t m;

    memset(&m, 0, sizeof m);
    m.type = MSG_BYE;
    (void)pm_wire_send(l, &m, now);
}

static void pm_drain_links(pm *pm)
{
    uint64_t deadline = loop_now_ms(pm->loop) + PM_DRAIN_MS;

    while (pm->links && loop_now_ms(pm->loop) < deadline) {
        (void)loop_run_once(pm->loop, 10);
    }
    while (pm->links) {
        LOG_W(PM_MOD, "session non fermée à la libération du manager");
        pm->links->pm = NULL;
        pm->links = pm->links->lnext;
        if (pm->links) {
            pm->links->lprev = NULL;
        }
    }
}

void pm_free(pm_t *pm)
{
    pm_link_t *l;
    uint64_t now;

    if (!pm) {
        return;
    }
    pm->closing = 1;
    now = loop_now_ms(pm->loop);
    loop_timer_cancel(pm->loop, pm->tick_id);
    tcp_listener_close(pm->listener);
    disc_free(pm->disc);
    for (l = pm->links; l; l = l->lnext) {
        if (l->hello_ok) {
            link_say_bye(l, now);
        }
        sess_close(l->sess, 1);
    }
    pm_drain_links(pm);
    pm_peer_free_all(pm);
    free(pm);
}

uint16_t pm_listen_port(const pm_t *pm)
{
    return tcp_listener_port(pm->listener);
}

static int send_args_valid(const char *text, uint64_t id)
{
    size_t n;

    if (!text || id == 0) {
        return 0;
    }
    n = strlen(text);
    return n > 0 && n <= MSG_TEXT_MAX && msg_utf8_valid((const uint8_t *)text, n);
}

int pm_send(pm_t *pm, const uint8_t pub[32], uint64_t id, uint64_t ts_ms, const char *text)
{
    pm_peer_t *p;
    int rc;

    if (!pm || !pub || !send_args_valid(text, id)) {
        return -EINVAL;
    }
    p = pm_peer_find(pm, pub);
    if (!p) {
        return -ENOENT;
    }
    rc = pm_queue_push(pm, p, id, ts_ms, text);
    if (rc != 0) {
        return rc;
    }
    if (p->cur && p->cur->hello_ok) {
        pm_queue_pump(pm, p->cur, loop_now_ms(pm->loop));
    }
    return 0;
}

pm_state pm_peer_state(const pm_t *pm, const uint8_t pub[32])
{
    pm_peer_t *p = pm_peer_find((pm_t *)pm, pub);

    return p ? pm_peer_compute(p) : PM_OFFLINE;
}

int pm_add_known(pm_t *pm, const uint8_t pub[32], const char *name, const char *host, uint16_t port)
{
    pm_peer_t *p;

    if (!pm || !pub || (host && (port == 0 || strlen(host) >= DISC_HOST_MAX))) {
        return -EINVAL;
    }
    p = pm_peer_get_or_add(pm, pub);
    if (!p) {
        return -ENOMEM;
    }
    if (name) {
        snprintf(p->name, sizeof p->name, "%s", name);
    }
    if (host) {
        snprintf(p->host, sizeof p->host, "%s", host);
        p->port = port;
        p->has_addr = 1;
    }
    return 0;
}
