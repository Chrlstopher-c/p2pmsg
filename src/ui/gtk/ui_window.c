/* ui/gtk — fenêtre principale : colonne pairs (identité, liste, badges), conversation, cycle GtkApplication, évènements. */
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "ui/gtk/ui_app.h"

#define UI_PEERS_PANE_WIDTH 260
#define UI_WINDOW_W 1000
#define UI_WINDOW_H 640
#define UI_KEY_PREFIX 8
#define UI_FINGERPRINT_LEN 8
#define UI_BADGE_MAX 99

static const char *peer_display_name(const app_peer *p, char *buf, size_t cap)
{
    if (p->name[0] != '\0') {
        return p->name;
    }
    snprintf(buf, cap, "%.*s", UI_KEY_PREFIX, p->key);
    return buf;
}

static void set_peer_dot_class(GtkWidget *dot, app_peer_state st)
{
    gtk_widget_remove_css_class(dot, "status-connected");
    gtk_widget_remove_css_class(dot, "status-connecting");
    gtk_widget_remove_css_class(dot, "status-offline");
    gtk_widget_add_css_class(dot, ui_peer_css_class(st));
}

static void set_badge(GtkWidget *badge, unsigned count)
{
    char text[8];

    if (count == 0) {
        gtk_widget_set_visible(badge, FALSE);
        return;
    }
    snprintf(text, sizeof text, "%u", count > UI_BADGE_MAX ? UI_BADGE_MAX : count);
    gtk_label_set_text(GTK_LABEL(badge), text);
    gtk_widget_set_visible(badge, TRUE);
}

void ui_window_add_peer_row(ui_ctx *ctx, int idx)
{
    const app_peer *p = &ctx->model.peers[idx];
    char fallback[UI_KEY_PREFIX + 1];
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *name = gtk_label_new(peer_display_name(p, fallback, sizeof fallback));
    GtkWidget *badge = gtk_label_new("");

    gtk_widget_add_css_class(dot, "status-dot");
    set_peer_dot_class(dot, p->state);
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(name, TRUE);
    gtk_widget_add_css_class(name, "peer-name");
    gtk_widget_add_css_class(badge, "badge");
    set_badge(badge, ctx->model.unread[idx]);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_box_append(GTK_BOX(box), dot);
    gtk_box_append(GTK_BOX(box), name);
    gtk_box_append(GTK_BOX(box), badge);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(ctx->peer_list), row);
}

void ui_window_refresh_peer(ui_ctx *ctx, int idx)
{
    const app_peer *p = &ctx->model.peers[idx];
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ctx->peer_list), idx);
    GtkWidget *box = row != NULL ? gtk_list_box_row_get_child(row) : NULL;
    GtkWidget *dot = box != NULL ? gtk_widget_get_first_child(box) : NULL;
    GtkWidget *name = dot != NULL ? gtk_widget_get_next_sibling(dot) : NULL;
    GtkWidget *badge = name != NULL ? gtk_widget_get_next_sibling(name) : NULL;
    char fallback[UI_KEY_PREFIX + 1];

    if (dot == NULL || name == NULL || badge == NULL) {
        return;
    }
    set_peer_dot_class(dot, p->state);
    gtk_label_set_text(GTK_LABEL(name), peer_display_name(p, fallback, sizeof fallback));
    set_badge(badge, ctx->model.unread[idx]);
}

void ui_window_select_peer_idx(ui_ctx *ctx, int idx)
{
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ctx->peer_list), idx);

    if (row != NULL) {
        gtk_list_box_select_row(GTK_LIST_BOX(ctx->peer_list), row);
    }
}

static void on_peer_selected(GtkListBox *box, GtkListBoxRow *row, gpointer ud)
{
    ui_ctx *ctx = ud;
    int idx;

    (void)box;
    if (row == NULL) {
        return;
    }
    idx = gtk_list_box_row_get_index(row);
    if (idx < 0 || (size_t)idx >= ctx->model.n_peers) {
        return;
    }
    ui_model_clear_unread(&ctx->model, idx);
    ui_window_refresh_peer(ctx, idx);
    ui_conv_select(ctx, ctx->model.peers[idx].key);
}

static void on_copy_clicked(GtkButton *btn, gpointer ud)
{
    ui_ctx *ctx = ud;
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(btn));

    gdk_clipboard_set_text(gdk_display_get_clipboard(display), app_self_key(ctx->app));
}

static GtkWidget *build_identity_header(ui_ctx *ctx)
{
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *name = gtk_label_new(ctx->self_name);
    GtkWidget *fp = gtk_label_new("");
    GtkWidget *copy = gtk_button_new_from_icon_name("edit-copy-symbolic");
    char fp_text[UI_FINGERPRINT_LEN + 16];

    snprintf(fp_text, sizeof fp_text, "empreinte %.*s", UI_FINGERPRINT_LEN, app_self_key(ctx->app));
    gtk_label_set_text(GTK_LABEL(fp), fp_text);
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(fp), 0.0f);
    gtk_widget_add_css_class(name, "identity-name");
    gtk_widget_add_css_class(fp, "bubble-meta");
    gtk_widget_add_css_class(copy, "flat");
    gtk_widget_set_tooltip_text(copy, "Copier la clé publique complète");
    gtk_widget_set_hexpand(texts, TRUE);
    gtk_box_append(GTK_BOX(texts), name);
    gtk_box_append(GTK_BOX(texts), fp);
    gtk_box_append(GTK_BOX(head), texts);
    gtk_box_append(GTK_BOX(head), copy);
    g_signal_connect(copy, "clicked", G_CALLBACK(on_copy_clicked), ctx);
    return head;
}

static GtkWidget *build_peers_pane(ui_ctx *ctx)
{
    GtkWidget *pane = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *scroll = gtk_scrolled_window_new();
    GtkWidget *hint = gtk_label_new("");

    ctx->peer_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx->peer_list), GTK_SELECTION_SINGLE);
    g_signal_connect(ctx->peer_list, "row-selected", G_CALLBACK(on_peer_selected), ctx);
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_widget_add_css_class(hint, "bubble-meta");
    gtk_widget_set_margin_start(hint, 10);
    gtk_widget_set_margin_end(hint, 10);
    gtk_widget_set_margin_top(hint, 8);
    ctx->peer_hint = hint;
    gtk_list_box_set_placeholder(GTK_LIST_BOX(ctx->peer_list), hint);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), ctx->peer_list);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(pane), build_identity_header(ctx));
    gtk_box_append(GTK_BOX(pane), scroll);
    gtk_widget_set_size_request(pane, UI_PEERS_PANE_WIDTH, -1);
    gtk_widget_set_margin_start(pane, 8);
    gtk_widget_set_margin_top(pane, 8);
    gtk_widget_set_margin_bottom(pane, 8);
    return pane;
}

static void update_peer_hint(ui_ctx *ctx)
{
    char text[UI_PATH_MAX + 64];

    snprintf(text, sizeof text, "Aucun pair — ajoutez-les dans %s", ctx->peers_file);
    gtk_label_set_text(GTK_LABEL(ctx->peer_hint), text);
}

static void build_window(ui_ctx *ctx, GtkApplication *gapp)
{
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    char title[UI_NAME_MAX + 16];

    ctx->window = gtk_application_window_new(gapp);
    snprintf(title, sizeof title, "p2pmsg — %s", ctx->self_name);
    gtk_window_set_title(GTK_WINDOW(ctx->window), title);
    gtk_window_set_default_size(GTK_WINDOW(ctx->window), UI_WINDOW_W, UI_WINDOW_H);
    gtk_paned_set_start_child(GTK_PANED(paned), build_peers_pane(ctx));
    gtk_paned_set_end_child(GTK_PANED(paned), ui_conv_build_pane(ctx));
    gtk_paned_set_position(GTK_PANED(paned), UI_PEERS_PANE_WIDTH + 10);
    gtk_window_set_child(GTK_WINDOW(ctx->window), paned);
    update_peer_hint(ctx);
    ui_conv_refresh_header(ctx);
}

static void apply_peer(ui_ctx *ctx, const app_peer *p)
{
    int is_new = 0;
    int idx = ui_model_upsert_peer(&ctx->model, p, &is_new);

    if (idx < 0) {
        return;
    }
    if (is_new) {
        ui_window_add_peer_row(ctx, idx);
        if (ui_model_current(&ctx->model)[0] == '\0') {
            ui_window_select_peer_idx(ctx, idx);
        }
    } else {
        ui_window_refresh_peer(ctx, idx);
    }
    if (strcmp(ui_model_current(&ctx->model), p->key) == 0) {
        ui_conv_refresh_header(ctx);
    }
}

static void populate_peers(ui_ctx *ctx)
{
    app_peer snapshot[UI_PEERS_MAX];
    int n = app_peers(ctx->app, snapshot, UI_PEERS_MAX);

    for (int i = 0; i < n; i++) {
        apply_peer(ctx, &snapshot[i]);
    }
    if (n > 0) {
        ui_window_select_peer_idx(ctx, 0);
    }
}

static void apply_message(ui_ctx *ctx, const app_message *m)
{
    int idx;

    if (strcmp(m->peer, ui_model_current(&ctx->model)) == 0) {
        ui_conv_append(ctx, m);
        return;
    }
    idx = ui_model_find_peer(&ctx->model, m->peer);
    if (idx >= 0) {
        ui_model_add_unread(&ctx->model, idx);
        ui_window_refresh_peer(ctx, idx);
    }
}

static gboolean on_term_signal(gpointer ud)
{
    g_application_quit(G_APPLICATION(ud));
    return G_SOURCE_REMOVE;
}

void ui_window_apply(ui_ctx *ctx, const ui_event *ev)
{
    switch (ev->kind) {
    case UI_EV_PEER:
        apply_peer(ctx, &ev->peer);
        break;
    case UI_EV_MESSAGE:
        apply_message(ctx, &ev->msg);
        break;
    case UI_EV_STATUS:
        ui_conv_set_status(ctx, ev->msg.peer, ev->msg.id, ev->msg.status);
        break;
    }
}

void ui_window_on_activate(GtkApplication *gapp, gpointer ud)
{
    ui_ctx *ctx = ud;

    if (ctx->window != NULL) {
        gtk_window_present(GTK_WINDOW(ctx->window));
        return;
    }
    ui_css_apply();
    build_window(ctx, gapp);
    if (ui_bridge_start(ctx) != 0) {
        ctx->failed = 1;
        g_application_quit(G_APPLICATION(gapp));
        return;
    }
    ctx->started = 1;
    populate_peers(ctx);
    g_unix_signal_add(SIGTERM, on_term_signal, gapp);
    g_unix_signal_add(SIGINT, on_term_signal, gapp);
    gtk_window_present(GTK_WINDOW(ctx->window));
}

void ui_window_on_shutdown(GtkApplication *gapp, gpointer ud)
{
    ui_ctx *ctx = ud;

    (void)gapp;
    if (ctx->started) {
        ui_bridge_stop(ctx);
    }
    ctx->shutting = 1;
}
