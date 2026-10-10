/* ui/gtk — volet conversation : bulles (GtkListBox), historique au changement de pair, saisie (Entrée / Maj+Entrée). */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

#include "ui/gtk/ui_app.h"

#define UI_TIME_BUF 8
#define UI_CONV_KEY_PREFIX 8
#define UI_INPUT_HINT \
    "Message — Entrée pour envoyer, Maj+Entrée pour une nouvelle ligne"

static gboolean scroll_bottom_idle_cb(gpointer ud);

static void set_status_label(GtkWidget *label, app_msg_status st)
{
    gtk_label_set_text(GTK_LABEL(label), ui_status_glyph(st));
    if (st == APP_MSG_FAILED) {
        gtk_widget_add_css_class(label, "status-failed");
    } else {
        gtk_widget_remove_css_class(label, "status-failed");
    }
}

static GtkWidget *make_meta_row(const ui_bubble *b, GtkWidget **status_out)
{
    char hhmm[UI_TIME_BUF] = "";
    GtkWidget *meta = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *time_lbl;

    (void)ui_format_time(b->ts_ms, hhmm, sizeof hhmm);
    time_lbl = gtk_label_new(hhmm);
    gtk_widget_add_css_class(time_lbl, "bubble-meta");
    *status_out = gtk_label_new("");
    gtk_widget_add_css_class(*status_out, "bubble-meta");
    set_status_label(*status_out, b->status);
    gtk_box_append(GTK_BOX(meta), time_lbl);
    gtk_box_append(GTK_BOX(meta), *status_out);
    if (!b->outgoing) {
        gtk_widget_set_visible(*status_out, FALSE);
    }
    gtk_widget_set_halign(meta, b->outgoing ? GTK_ALIGN_END : GTK_ALIGN_START);
    return meta;
}

static GtkWidget *make_bubble_box(const ui_bubble *b, GtkWidget **status_out)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *text = gtk_label_new(b->text);

    gtk_label_set_wrap(GTK_LABEL(text), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(text), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
    gtk_label_set_max_width_chars(GTK_LABEL(text), 60);
    gtk_label_set_selectable(GTK_LABEL(text), TRUE);
    gtk_widget_add_css_class(box, "bubble");
    gtk_widget_add_css_class(box, b->outgoing ? "bubble-out" : "bubble-in");
    gtk_widget_set_halign(box, b->outgoing ? GTK_ALIGN_END : GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), text);
    gtk_box_append(GTK_BOX(box), make_meta_row(b, status_out));
    return box;
}

static GtkListBoxRow *make_row(const ui_bubble *b)
{
    GtkWidget *status = NULL;
    GtkWidget *row_w = make_bubble_box(b, &status);
    GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_list_box_row_new());

    gtk_list_box_row_set_child(row, row_w);
    gtk_list_box_row_set_activatable(row, FALSE);
    g_object_set_data(G_OBJECT(row), "status", status);
    return row;
}

static void remove_first_row(GtkWidget *list)
{
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0);

    if (row != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(list), GTK_WIDGET(row));
    }
}

static void clear_rows(GtkWidget *list)
{
    while (gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0) != NULL) {
        remove_first_row(list);
    }
}

static void rebuild_rows(ui_ctx *ctx)
{
    clear_rows(ctx->conv_list);
    for (size_t i = 0; i < ctx->model.n_bubbles; i++) {
        gtk_list_box_append(GTK_LIST_BOX(ctx->conv_list), GTK_WIDGET(make_row(&ctx->model.bubbles[i])));
    }
    g_idle_add(scroll_bottom_idle_cb, ctx);
}

static gboolean scroll_bottom_idle_cb(gpointer ud)
{
    ui_ctx *ctx = ud;
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(ctx->conv_scroll));

    gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
    return G_SOURCE_REMOVE;
}

static void on_history(const app_message *m, void *ud)
{
    ui_ctx *ctx = ud;

    (void)ui_model_prepend(&ctx->model, m);
}

void ui_conv_select(ui_ctx *ctx, const char *key)
{
    if (key == NULL || key[0] == '\0') {
        ui_model_set_current(&ctx->model, "");
        clear_rows(ctx->conv_list);
        ui_conv_refresh_header(ctx);
        return;
    }
    ui_model_set_current(&ctx->model, key);
    (void)app_history(ctx->app, key, g_get_real_time() / 1000 + 1, UI_HISTORY_PAGE, on_history, ctx);
    rebuild_rows(ctx);
    ui_conv_refresh_header(ctx);
    gtk_widget_grab_focus(ctx->input_view);
}

static void apply_header_status(GtkWidget *label, app_peer_state st)
{
    gtk_widget_remove_css_class(label, "status-connected");
    gtk_widget_remove_css_class(label, "status-connecting");
    gtk_widget_remove_css_class(label, "status-offline");
    gtk_widget_add_css_class(label, ui_peer_css_class(st));
}

void ui_conv_refresh_header(ui_ctx *ctx)
{
    const char *cur = ui_model_current(&ctx->model);
    int idx = cur[0] != '\0' ? ui_model_find_peer(&ctx->model, cur) : -1;
    char fallback[UI_CONV_KEY_PREFIX + 1];
    const char *name = "";

    if (idx < 0) {
        gtk_label_set_text(GTK_LABEL(ctx->conv_title), "");
        gtk_label_set_text(GTK_LABEL(ctx->conv_status), "");
        gtk_label_set_text(GTK_LABEL(ctx->conv_hint), "Sélectionnez un pair");
        gtk_widget_set_sensitive(ctx->input_view, FALSE);
        return;
    }
    if (ctx->model.peers[idx].name[0] != '\0') {
        name = ctx->model.peers[idx].name;
    } else {
        snprintf(fallback, sizeof fallback, "%.*s", UI_CONV_KEY_PREFIX, cur);
        name = fallback;
    }
    gtk_label_set_text(GTK_LABEL(ctx->conv_title), name);
    gtk_label_set_text(GTK_LABEL(ctx->conv_status), ui_peer_status_text(ctx->model.peers[idx].state));
    apply_header_status(ctx->conv_status, ctx->model.peers[idx].state);
    gtk_label_set_text(GTK_LABEL(ctx->conv_hint), "Aucun message");
    gtk_widget_set_sensitive(ctx->input_view, TRUE);
}

void ui_conv_append(ui_ctx *ctx, const app_message *msg)
{
    int evicted = 0;
    int idx;

    if (strcmp(msg->peer, ui_model_current(&ctx->model)) != 0) {
        return;
    }
    if (!msg->outgoing && ui_model_find_id(&ctx->model, msg->id) >= 0) {
        return;
    }
    idx = ui_model_append(&ctx->model, msg, &evicted);
    if (idx < 0) {
        return;
    }
    if (evicted) {
        remove_first_row(ctx->conv_list);
    }
    gtk_list_box_append(GTK_LIST_BOX(ctx->conv_list), GTK_WIDGET(make_row(&ctx->model.bubbles[idx])));
    g_idle_add(scroll_bottom_idle_cb, ctx);
}

void ui_conv_set_status(ui_ctx *ctx, const char *peer, uint64_t id, app_msg_status st)
{
    int idx = ui_model_set_status(&ctx->model, peer, id, st);
    GtkListBoxRow *row;
    GtkWidget *label;

    if (idx < 0) {
        return;
    }
    row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ctx->conv_list), idx);
    label = row != NULL ? g_object_get_data(G_OBJECT(row), "status") : NULL;
    if (label != NULL) {
        set_status_label(label, st);
    }
}

static void send_input(ui_ctx *ctx)
{
    GtkTextIter start;
    GtkTextIter end;
    char *text;
    uint64_t id = 0;
    int rc;
    app_message msg;

    gtk_text_buffer_get_bounds(ctx->input_buf, &start, &end);
    text = gtk_text_buffer_get_text(ctx->input_buf, &start, &end, FALSE);
    if (!ui_text_sendable(text) || ui_model_current(&ctx->model)[0] == '\0') {
        g_free(text);
        return;
    }
    rc = app_send(ctx->app, ui_model_current(&ctx->model), text, &id);
    memset(&msg, 0, sizeof msg);
    snprintf(msg.peer, sizeof msg.peer, "%s", ui_model_current(&ctx->model));
    msg.id = rc == 0 ? id : 0;
    msg.outgoing = 1;
    msg.ts_ms = g_get_real_time() / 1000;
    msg.status = rc == 0 ? APP_MSG_PENDING : APP_MSG_FAILED;
    msg.text = text;
    ui_conv_append(ctx, &msg);
    gtk_text_buffer_set_text(ctx->input_buf, "", -1);
    g_free(text);
}

static void on_buffer_changed(GtkTextBuffer *buf, gpointer ud)
{
    GtkWidget *hint = ud;

    gtk_widget_set_visible(hint, gtk_text_buffer_get_char_count(buf) == 0);
}

static gboolean on_input_key(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                             GdkModifierType state, gpointer ud)
{
    gboolean is_enter = keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;

    (void)ctrl;
    (void)keycode;
    if (ui_key_decide(is_enter, shift) != UI_KEY_SEND) {
        return FALSE;
    }
    send_input(ud);
    return TRUE;
}

static GtkWidget *build_input_hint(ui_ctx *ctx)
{
    GtkWidget *hint = gtk_label_new(UI_INPUT_HINT);

    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_widget_set_halign(hint, GTK_ALIGN_FILL);
    gtk_widget_set_valign(hint, GTK_ALIGN_START);
    gtk_widget_set_can_target(hint, FALSE);
    gtk_widget_set_margin_start(hint, 14);
    gtk_widget_set_margin_top(hint, 8);
    gtk_widget_add_css_class(hint, "bubble-meta");
    ctx->input_hint = hint;
    return hint;
}

static GtkWidget *build_input(ui_ctx *ctx)
{
    GtkWidget *view = gtk_text_view_new();
    GtkWidget *scroll = gtk_scrolled_window_new();
    GtkWidget *overlay = gtk_overlay_new();
    GtkEventController *keys = gtk_event_controller_key_new();

    ctx->input_view = view;
    ctx->input_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_margin_start(view, 8);
    gtk_widget_set_margin_end(view, 8);
    gtk_widget_set_margin_top(view, 6);
    gtk_widget_set_margin_bottom(view, 6);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 40);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 120);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_widget_add_css_class(scroll, "input");
    gtk_overlay_set_child(GTK_OVERLAY(overlay), scroll);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), build_input_hint(ctx));
    g_signal_connect(ctx->input_buf, "changed", G_CALLBACK(on_buffer_changed), ctx->input_hint);
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_input_key), ctx);
    gtk_widget_add_controller(view, keys);
    gtk_widget_set_sensitive(view, FALSE);
    return overlay;
}

static GtkWidget *build_conv_header(ui_ctx *ctx)
{
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    ctx->conv_title = gtk_label_new("");
    ctx->conv_status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ctx->conv_title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(ctx->conv_title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(ctx->conv_title, TRUE);
    gtk_widget_add_css_class(ctx->conv_title, "identity-name");
    gtk_widget_add_css_class(ctx->conv_status, "status-text");
    gtk_box_append(GTK_BOX(head), ctx->conv_title);
    gtk_box_append(GTK_BOX(head), ctx->conv_status);
    gtk_widget_set_margin_top(head, 8);
    return head;
}

GtkWidget *ui_conv_build_pane(ui_ctx *ctx)
{
    GtkWidget *pane = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *list = gtk_list_box_new();

    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    ctx->conv_list = list;
    ctx->conv_hint = gtk_label_new("Sélectionnez un pair");
    gtk_widget_add_css_class(ctx->conv_hint, "bubble-meta");
    gtk_widget_set_margin_top(ctx->conv_hint, 24);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(list), ctx->conv_hint);
    ctx->conv_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ctx->conv_scroll), list);
    gtk_widget_set_vexpand(ctx->conv_scroll, TRUE);
    gtk_box_append(GTK_BOX(pane), build_conv_header(ctx));
    gtk_box_append(GTK_BOX(pane), ctx->conv_scroll);
    gtk_box_append(GTK_BOX(pane), build_input(ctx));
    gtk_widget_set_margin_start(pane, 8);
    gtk_widget_set_margin_end(pane, 8);
    gtk_widget_set_margin_bottom(pane, 8);
    return pane;
}
