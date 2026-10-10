/* ui/gtk — pont réseau → GTK : callbacks app_* (thread réseau) copiés puis livrés par g_idle_add. */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/gtk/ui_app.h"

static void ui_event_free(ui_event *ev)
{
    g_free(ev->text);
    g_free(ev);
}

static gboolean ui_event_dispatch(gpointer data)
{
    ui_event *ev = data;

    if (!ev->ctx->shutting) {
        ui_window_apply(ev->ctx, ev);
    }
    ui_event_free(ev);
    return G_SOURCE_REMOVE;
}

static void ui_event_post(ui_ctx *ctx, ui_event *ev)
{
    ev->ctx = ctx;
    g_idle_add(ui_event_dispatch, ev);
}

static ui_event *ui_event_new(ui_event_kind kind, ui_ctx *ctx)
{
    ui_event *ev = g_new0(ui_event, 1);

    ev->kind = kind;
    ev->ctx = ctx;
    return ev;
}

static void on_peer_evt(const app_peer *p, void *ud)
{
    ui_event *ev = ui_event_new(UI_EV_PEER, ud);

    ev->peer = *p;
    ui_event_post(ud, ev);
}

static void on_message_evt(const app_message *m, void *ud)
{
    ui_event *ev = ui_event_new(UI_EV_MESSAGE, ud);

    ev->msg = *m;
    ev->text = g_strdup(m->text != NULL ? m->text : "");
    ev->msg.text = ev->text;
    ui_event_post(ud, ev);
}

static void on_status_evt(const char *peer, uint64_t id, app_msg_status st, void *ud)
{
    ui_event *ev = ui_event_new(UI_EV_STATUS, ud);

    snprintf(ev->msg.peer, sizeof ev->msg.peer, "%s", peer);
    ev->msg.id = id;
    ev->msg.status = st;
    ui_event_post(ud, ev);
}

static const app_events UI_EVENTS = {
    .on_peer = on_peer_evt,
    .on_message = on_message_evt,
    .on_status = on_status_evt,
};

static void resolve_paths(ui_ctx *ctx, const ui_options *opts)
{
    const char *home = g_get_home_dir();
    const char *xdg = g_getenv("XDG_DATA_HOME");
    char dir[UI_PATH_MAX - 6];

    if (opts->dir != NULL) {
        snprintf(dir, sizeof dir, "%s", opts->dir);
    } else if (xdg != NULL && xdg[0] != '\0') {
        snprintf(dir, sizeof dir, "%s/p2pmsg", xdg);
    } else {
        snprintf(dir, sizeof dir, "%s/.local/share/p2pmsg", home != NULL ? home : ".");
    }
    snprintf(ctx->peers_file, sizeof ctx->peers_file, "%s/peers", dir);
    snprintf(ctx->self_name, sizeof ctx->self_name, "%s",
             opts->name != NULL ? opts->name : g_get_user_name());
}

int ui_bridge_open(ui_ctx *ctx, const ui_options *opts)
{
    app_config cfg = {0};

    resolve_paths(ctx, opts);
    cfg.dir = opts->dir;
    cfg.name = opts->name;
    cfg.listen_port = opts->port;
    cfg.enable_mdns = !opts->no_mdns;
    cfg.log_debug = opts->debug;
    ctx->app = app_new(&cfg, &UI_EVENTS, ctx);
    if (ctx->app == NULL) {
        fprintf(stderr, "p2pmsg-gtk : démarrage impossible (voir p2pmsg.log)\n");
        return -1;
    }
    return 0;
}

int ui_bridge_start(ui_ctx *ctx)
{
    int rc = app_start(ctx->app);

    if (rc != 0) {
        fprintf(stderr, "p2pmsg-gtk : écoute impossible (rc=%d)\n", rc);
    }
    return rc;
}

void ui_bridge_stop(ui_ctx *ctx)
{
    ctx->shutting = 1;
    if (ctx->app != NULL) {
        app_stop(ctx->app);
    }
}

void ui_bridge_close(ui_ctx *ctx)
{
    if (ctx->app != NULL) {
        app_free(ctx->app);
        ctx->app = NULL;
    }
}
