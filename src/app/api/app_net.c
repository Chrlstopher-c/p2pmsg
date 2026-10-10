/* app/api : thread réseau. Possède loop, pm et le hist_t réseau. Les callbacks pm s'exécutent ici ;
 * les commandes des autres threads arrivent par la file (drainée au wake). */
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "app/api/app_internal.h"
#include "core/log/log.h"

static void net_emit_status(app_t *a, const uint8_t pub[32], uint64_t id, app_msg_status st)
{
    char hex[APP_KEY_HEX];

    if (a->ev.on_status != NULL) {
        sess_key_to_hex(pub, hex);
        a->ev.on_status(hex, id, st, a->ud);
    }
}

static app_peer *find_peer(app_t *a, const char *hex)
{
    for (size_t i = 0; i < a->npeers; i++) {
        if (strcmp(a->peers[i].key, hex) == 0) {
            return &a->peers[i];
        }
    }
    return NULL;
}

static app_peer *add_peer(app_t *a, const char *hex)
{
    app_peer *p;

    if (a->npeers >= APP_PEERS_MAX) {
        LOG_W(APP_MOD, "snapshot des pairs plein, pair ignoré key=%.8s", hex);
        return NULL;
    }
    p = &a->peers[a->npeers++];
    memset(p, 0, sizeof *p);
    snprintf(p->key, sizeof p->key, "%s", hex);
    p->state = APP_OFFLINE;
    return p;
}

static void peers_set(app_t *a, const uint8_t pub[32], const char *name, app_peer_state st)
{
    char hex[APP_KEY_HEX];
    app_peer *p;

    sess_key_to_hex(pub, hex);
    pthread_mutex_lock(&a->peers_mu);
    p = find_peer(a, hex);
    if (p == NULL) {
        p = add_peer(a, hex);
    }
    if (p != NULL) {
        if (name != NULL) {
            snprintf(p->name, sizeof p->name, "%s", name);
        }
        p->state = st;
    }
    pthread_mutex_unlock(&a->peers_mu);
}

static app_peer_state map_state(pm_state st)
{
    switch (st) {
    case PM_CONNECTING: return APP_CONNECTING;
    case PM_CONNECTED:  return APP_CONNECTED;
    default:            return APP_OFFLINE;
    }
}

static void pm_on_peer(const uint8_t pub[32], const char *name, pm_state st, void *ud)
{
    app_t *a = ud;
    app_peer p;

    hist_upsert_peer(a->net_hist, pub, name != NULL ? name : "", app_now_ms());
    peers_set(a, pub, name, map_state(st));
    if (a->ev.on_peer != NULL) {
        memset(&p, 0, sizeof p);
        sess_key_to_hex(pub, p.key);
        snprintf(p.name, sizeof p.name, "%s", name != NULL ? name : "");
        p.state = map_state(st);
        a->ev.on_peer(&p, a->ud);
    }
}

static void pm_on_message(const uint8_t pub[32], uint64_t id, uint64_t ts_ms, const char *text, void *ud)
{
    app_t *a = ud;
    hist_msg m;
    app_message am;

    memset(&m, 0, sizeof m);
    memcpy(m.peer, pub, sizeof m.peer);
    m.id = id;
    m.dir = HIST_IN;
    m.ts_ms = ts_ms;
    m.body = text;
    m.status = HIST_RECEIVED;
    if (hist_add(a->net_hist, &m) < 0) {
        LOG_E(APP_MOD, "reçu non persisté id=%llx", (unsigned long long)id);
    }
    if (a->ev.on_message != NULL) {
        memset(&am, 0, sizeof am);
        sess_key_to_hex(pub, am.peer);
        am.id = id;
        am.outgoing = 0;
        am.ts_ms = ts_ms;
        am.text = text;
        am.status = APP_MSG_RECEIVED;
        a->ev.on_message(&am, a->ud);
    }
}

static void pm_on_delivered(const uint8_t pub[32], uint64_t id, void *ud)
{
    app_t *a = ud;

    if (hist_set_status(a->net_hist, pub, id, HIST_OUT, HIST_DELIVERED) < 0) {
        LOG_E(APP_MOD, "statut delivered non persisté id=%llx", (unsigned long long)id);
    }
    net_emit_status(a, pub, id, APP_MSG_DELIVERED);
}

static void pm_on_failed(const uint8_t pub[32], uint64_t id, void *ud)
{
    app_t *a = ud;

    if (hist_set_status(a->net_hist, pub, id, HIST_OUT, HIST_FAILED) < 0) {
        LOG_E(APP_MOD, "statut failed non persisté id=%llx", (unsigned long long)id);
    }
    net_emit_status(a, pub, id, APP_MSG_FAILED);
}

static void do_send(app_t *a, const app_cmd *c)
{
    hist_msg m;
    int rc;

    memset(&m, 0, sizeof m);
    memcpy(m.peer, c->peer, sizeof m.peer);
    m.id = c->id;
    m.dir = HIST_OUT;
    m.ts_ms = c->ts_ms;
    m.body = c->text;
    m.status = HIST_PENDING;
    if (hist_add(a->net_hist, &m) < 0) {
        LOG_E(APP_MOD, "envoi non persisté id=%llx", (unsigned long long)c->id);
        net_emit_status(a, c->peer, c->id, APP_MSG_FAILED);
        return;
    }
    rc = pm_send(a->pm, c->peer, c->id, c->ts_ms, c->text);
    if (rc != 0) {
        LOG_W(APP_MOD, "envoi refusé id=%llx rc=%d", (unsigned long long)c->id, rc);
        hist_set_status(a->net_hist, c->peer, c->id, HIST_OUT, HIST_FAILED);
        net_emit_status(a, c->peer, c->id, APP_MSG_FAILED);
    }
}

static void drain_commands(app_t *a)
{
    app_cmd *list = aq_take_all(&a->queue);

    while (list != NULL) {
        app_cmd *next = list->next;

        if (list->kind == CMD_SEND) {
            do_send(a, list);
        }
        free(list->text);
        free(list);
        list = next;
    }
}

static void wake_cb(loop_t *l, void *ud)
{
    app_t *a = ud;

    drain_commands(a);
    if (atomic_load(&a->stop_req)) {
        loop_stop(l);
    }
}

static int peer_cb(const hist_peer *p, void *ud)
{
    app_t *a = ud;

    peers_set(a, p->pubkey, p->name, APP_OFFLINE);
    if (pm_add_known(a->pm, p->pubkey, p->name, NULL, 0) < 0) {
        LOG_W(APP_MOD, "pair persisté non réinjecté");
    }
    return 0;
}

static int resume_cb(const hist_msg *m, void *ud)
{
    app_t *a = ud;
    int rc = pm_send(a->pm, m->peer, m->id, m->ts_ms, m->body);

    if (rc != 0) {
        LOG_W(APP_MOD, "reprise impossible, reste pending id=%llx rc=%d", (unsigned long long)m->id, rc);
    }
    return 0;
}

static void *net_thread(void *arg)
{
    app_t *a = arg;

    if (hist_peers(a->net_hist, peer_cb, a) < 0) {
        LOG_E(APP_MOD, "lecture des pairs persistés échouée");
    }
    if (hist_pending(a->net_hist, resume_cb, a) < 0) {
        LOG_E(APP_MOD, "lecture des messages pending échouée");
    }
    if (loop_run(a->loop) < 0) {
        LOG_E(APP_MOD, "boucle réseau arrêtée sur erreur");
    }
    pm_free(a->pm);
    a->pm = NULL;
    return NULL;
}

int net_init(app_t *a, const app_config *cfg)
{
    char hist_path[APP_FILE_PATH_MAX];
    pm_config pc;
    pm_events pe;

    a->loop = loop_new();
    if (a->loop == NULL) {
        LOG_E(APP_MOD, "boucle réseau impossible");
        return -ENOMEM;
    }
    snprintf(hist_path, sizeof hist_path, "%s/history.db", a->dir);
    a->net_hist = hist_open(hist_path);
    if (a->net_hist == NULL) {
        return -EIO;
    }
    memset(&pc, 0, sizeof pc);
    pc.identity = &a->id;
    pc.name = a->name;
    pc.listen_host = cfg->listen_host;
    pc.listen_port = cfg->listen_port != 0 ? cfg->listen_port : APP_DEFAULT_PORT;
    pc.disc.peers_file = a->peers_path[0] != '\0' ? a->peers_path : NULL;
    pc.disc.enable_mdns = a->enable_mdns;
    pc.time_div = 1;
    memset(&pe, 0, sizeof pe);
    pe.on_peer = pm_on_peer;
    pe.on_message = pm_on_message;
    pe.on_delivered = pm_on_delivered;
    pe.on_failed = pm_on_failed;
    a->pm = pm_new(a->loop, &pc, &pe, a);
    if (a->pm == NULL) {
        LOG_E(APP_MOD, "gestionnaire de pairs impossible port=%u", (unsigned)pc.listen_port);
        return -EADDRINUSE;
    }
    a->port = pm_listen_port(a->pm);
    return loop_set_wake(a->loop, wake_cb, a);
}

int net_start(app_t *a)
{
    int rc = pthread_create(&a->thread, NULL, net_thread, a);

    if (rc != 0) {
        LOG_E(APP_MOD, "thread réseau impossible rc=%d", rc);
        return -rc;
    }
    a->running = 1;
    return 0;
}

void net_stop(app_t *a)
{
    if (!a->running) {
        return;
    }
    atomic_store(&a->stop_req, 1);
    loop_wake(a->loop);
    pthread_join(a->thread, NULL);
    a->running = 0;
}

void net_release(app_t *a)
{
    if (a->pm != NULL) {
        pm_free(a->pm);
        a->pm = NULL;
    }
    if (a->loop != NULL) {
        loop_free(a->loop);
        a->loop = NULL;
    }
    if (a->net_hist != NULL) {
        hist_close(a->net_hist);
        a->net_hist = NULL;
    }
}
