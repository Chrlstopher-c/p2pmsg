/* app/api — API publique (app.h). Toutes les fonctions app_* sont thread-safe ; les callbacks
 * s'exécutent dans le thread réseau. Cycle de vie : app_new → app_start → app_stop → app_free. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app/api/app.h"
#include "app/api/app_internal.h"
#include "core/log/log.h"

typedef struct {
    void (*cb)(const app_message *m, void *ud);
    void *ud;
} hist_ctx;

static int setup_sync(app_t *a)
{
    if (aq_init(&a->queue) != 0) {
        return -ENOMEM;
    }
    if (pthread_mutex_init(&a->hist_mu, NULL) != 0 || pthread_mutex_init(&a->peers_mu, NULL) != 0) {
        return -ENOMEM;
    }
    a->sync_ready = 1;
    return 0;
}

static hist_t *open_ui_hist(const app_t *a)
{
    char path[APP_FILE_PATH_MAX];

    snprintf(path, sizeof path, "%s/history.db", a->dir);
    return hist_open(path);
}

static int app_setup(app_t *a, const app_config *cfg, const app_events *ev, void *ud)
{
    char path[APP_FILE_PATH_MAX];

    a->ud = ud;
    if (ev != NULL) {
        a->ev = *ev;
    }
    if (setup_sync(a) != 0 || app_resolve_dir(cfg->dir, a->dir, sizeof a->dir) != 0) {
        return -EINVAL;
    }
    if (app_make_dir(a->dir) != 0) {
        return -EIO;
    }
    snprintf(path, sizeof path, "%s/p2pmsg.log", a->dir);
    log_init(cfg->log_debug ? LOG_DEBUG : LOG_INFO, path);
    snprintf(path, sizeof path, "%s/identity.key", a->dir);
    if (sess_identity_load_or_create(path, &a->id) != 0) {
        LOG_E(APP_MOD, "identité illisible path=%s", path);
        return -EIO;
    }
    sess_key_to_hex(a->id.pub, a->self_hex);
    if (cfg->name != NULL) {
        snprintf(a->name, sizeof a->name, "%s", cfg->name);
    } else {
        app_user_name(a->name, sizeof a->name);
    }
    snprintf(path, sizeof path, "%s/peers", a->dir);
    if (access(path, R_OK) == 0) {
        snprintf(a->peers_path, sizeof a->peers_path, "%s", path);
    }
    a->enable_mdns = cfg->enable_mdns;
    a->ui_hist = open_ui_hist(a);
    if (a->ui_hist == NULL) {
        return -EIO;
    }
    return net_init(a, cfg);
}

app_t *app_new(const app_config *cfg, const app_events *ev, void *ud)
{
    app_t *a;

    if (cfg == NULL) {
        return NULL;
    }
    a = calloc(1, sizeof *a);
    if (a == NULL) {
        return NULL;
    }
    if (app_setup(a, cfg, ev, ud) != 0) {
        app_free(a);
        return NULL;
    }
    return a;
}

int app_start(app_t *a)
{
    if (a == NULL) {
        return -EINVAL;
    }
    if (a->running) {
        return -EALREADY;
    }
    LOG_I(APP_MOD, "démarrage port=%u", (unsigned)a->port);
    return net_start(a);
}

void app_stop(app_t *a)
{
    if (a != NULL) {
        net_stop(a);
    }
}

void app_free(app_t *a)
{
    if (a == NULL) {
        return;
    }
    app_stop(a);
    net_release(a);
    if (a->ui_hist != NULL) {
        hist_close(a->ui_hist);
    }
    if (a->sync_ready) {
        aq_destroy(&a->queue);
        pthread_mutex_destroy(&a->hist_mu);
        pthread_mutex_destroy(&a->peers_mu);
    }
    free(a);
}

const char *app_self_key(const app_t *a)
{
    return a->self_hex;
}

uint16_t app_listen_port(const app_t *a)
{
    return a->port;
}

int app_send(app_t *a, const char *peer_hex, const char *text, uint64_t *out_id)
{
    app_cmd *c;
    size_t len;
    uint64_t id;

    if (a == NULL || peer_hex == NULL || text == NULL) {
        return -EINVAL;
    }
    len = strlen(text);
    if (len == 0 || len > APP_TEXT_MAX) {
        return -EINVAL;
    }
    c = calloc(1, sizeof *c);
    if (c == NULL) {
        return -ENOMEM;
    }
    if (sess_key_from_hex(peer_hex, c->peer) != 0 || (id = app_random_id()) == 0) {
        free(c);
        return -EINVAL;
    }
    c->text = malloc(len + 1);
    if (c->text == NULL) {
        free(c);
        return -ENOMEM;
    }
    memcpy(c->text, text, len + 1);
    c->kind = CMD_SEND;
    c->id = id;
    c->ts_ms = app_now_ms();
    if (aq_push(&a->queue, c) != 0) {
        free(c->text);
        free(c);
        return -ENOSPC;
    }
    if (loop_wake(a->loop) != 0) {
        LOG_W(APP_MOD, "réveil de la boucle impossible, envoi au prochain tour");
    }
    if (out_id != NULL) {
        *out_id = id;
    }
    return 0;
}

int app_peers(app_t *a, app_peer *out, int max)
{
    int n = 0;

    if (a == NULL || max < 0 || (out == NULL && max > 0)) {
        return -EINVAL;
    }
    pthread_mutex_lock(&a->peers_mu);
    for (size_t i = 0; i < a->npeers && n < max; i++) {
        out[n++] = a->peers[i];
    }
    pthread_mutex_unlock(&a->peers_mu);
    return n;
}

static app_msg_status map_status(hist_status st)
{
    switch (st) {
    case HIST_DELIVERED: return APP_MSG_DELIVERED;
    case HIST_FAILED:    return APP_MSG_FAILED;
    case HIST_RECEIVED:  return APP_MSG_RECEIVED;
    default:             return APP_MSG_PENDING;
    }
}

static int hist_adapter(const hist_msg *m, void *ud)
{
    hist_ctx *ctx = ud;
    app_message am;

    memset(&am, 0, sizeof am);
    sess_key_to_hex(m->peer, am.peer);
    am.id = m->id;
    am.outgoing = m->dir == HIST_OUT;
    am.ts_ms = m->ts_ms;
    am.text = m->body;
    am.status = map_status(m->status);
    ctx->cb(&am, ctx->ud);
    return 0;
}

int app_history(app_t *a, const char *peer_hex, uint64_t before_ms, int limit,
                void (*cb)(const app_message *m, void *ud), void *ud)
{
    uint8_t pub[32];
    hist_ctx ctx = {.cb = cb, .ud = ud};
    int rc;

    if (a == NULL || peer_hex == NULL || cb == NULL || limit <= 0) {
        return -EINVAL;
    }
    if (sess_key_from_hex(peer_hex, pub) != 0) {
        return -EINVAL;
    }
    pthread_mutex_lock(&a->hist_mu);
    rc = hist_list(a->ui_hist, pub, before_ms, limit, hist_adapter, &ctx);
    pthread_mutex_unlock(&a->hist_mu);
    return rc;
}
