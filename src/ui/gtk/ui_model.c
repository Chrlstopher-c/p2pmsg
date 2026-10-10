/* ui/gtk — implémentation du modèle pur : pas de GTK, pas d'I/O hors localtime_r. */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ui/gtk/ui_model.h"

static void bubble_free(ui_bubble *b)
{
    free(b->text);
    b->text = NULL;
}

void ui_model_init(ui_model *m)
{
    memset(m, 0, sizeof *m);
}

void ui_model_clear_conv(ui_model *m)
{
    for (size_t i = 0; i < m->n_bubbles; i++) {
        bubble_free(&m->bubbles[i]);
    }
    m->n_bubbles = 0;
}

int ui_model_upsert_peer(ui_model *m, const app_peer *p, int *is_new)
{
    int idx = ui_model_find_peer(m, p->key);

    *is_new = 0;
    if (idx >= 0) {
        m->peers[idx] = *p;
        return idx;
    }
    if (m->n_peers >= UI_PEERS_MAX) {
        return -1;
    }
    m->peers[m->n_peers] = *p;
    *is_new = 1;
    return (int)m->n_peers++;
}

int ui_model_find_peer(const ui_model *m, const char *key)
{
    for (size_t i = 0; i < m->n_peers; i++) {
        if (strcmp(m->peers[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

const char *ui_model_current(const ui_model *m)
{
    return m->current;
}

void ui_model_set_current(ui_model *m, const char *key)
{
    ui_model_clear_conv(m);
    snprintf(m->current, sizeof m->current, "%s", key != NULL ? key : "");
}

static int bubble_fill(ui_bubble *b, const app_message *msg)
{
    b->text = strdup(msg->text != NULL ? msg->text : "");
    if (b->text == NULL) {
        return -1;
    }
    b->id = msg->id;
    b->outgoing = msg->outgoing;
    b->ts_ms = msg->ts_ms;
    b->status = msg->status;
    return 0;
}

int ui_model_append(ui_model *m, const app_message *msg, int *evicted)
{
    ui_bubble tmp;

    *evicted = 0;
    if (m->n_bubbles >= UI_CONV_MAX) {
        bubble_free(&m->bubbles[0]);
        memmove(&m->bubbles[0], &m->bubbles[1], (UI_CONV_MAX - 1) * sizeof m->bubbles[0]);
        m->n_bubbles--;
        *evicted = 1;
    }
    if (bubble_fill(&tmp, msg) != 0) {
        return -1;
    }
    m->bubbles[m->n_bubbles] = tmp;
    return (int)m->n_bubbles++;
}

int ui_model_prepend(ui_model *m, const app_message *msg)
{
    ui_bubble tmp;

    if (m->n_bubbles >= UI_CONV_MAX || bubble_fill(&tmp, msg) != 0) {
        return -1;
    }
    memmove(&m->bubbles[1], &m->bubbles[0], m->n_bubbles * sizeof m->bubbles[0]);
    m->bubbles[0] = tmp;
    m->n_bubbles++;
    return 0;
}

int ui_model_find_id(const ui_model *m, uint64_t id)
{
    for (size_t i = 0; i < m->n_bubbles; i++) {
        if (m->bubbles[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

int ui_model_set_status(ui_model *m, const char *peer, uint64_t id, app_msg_status st)
{
    int idx;

    if (strcmp(peer, m->current) != 0) {
        return -1;
    }
    idx = ui_model_find_id(m, id);
    if (idx < 0) {
        return -1;
    }
    m->bubbles[idx].status = st;
    return idx;
}

int ui_format_time(uint64_t ts_ms, char *out, size_t cap)
{
    time_t secs = (time_t)(ts_ms / 1000u);
    struct tm tm_local;

    if (cap < UI_TIME_LEN || localtime_r(&secs, &tm_local) == NULL) {
        return -1;
    }
    if (strftime(out, cap, "%H:%M", &tm_local) == 0) {
        return -1;
    }
    return 0;
}

const char *ui_status_glyph(app_msg_status st)
{
    switch (st) {
    case APP_MSG_PENDING:   return "…";
    case APP_MSG_DELIVERED: return "✓";
    case APP_MSG_FAILED:    return "!";
    default:                return "";
    }
}

const char *ui_peer_css_class(app_peer_state st)
{
    switch (st) {
    case APP_CONNECTED:  return "status-connected";
    case APP_CONNECTING: return "status-connecting";
    default:             return "status-offline";
    }
}

const char *ui_peer_status_text(app_peer_state st)
{
    switch (st) {
    case APP_CONNECTED:  return "connecté";
    case APP_CONNECTING: return "en connexion";
    default:             return "hors ligne";
    }
}

void ui_model_add_unread(ui_model *m, int idx)
{
    if (idx >= 0 && (size_t)idx < m->n_peers) {
        m->unread[idx]++;
    }
}

void ui_model_clear_unread(ui_model *m, int idx)
{
    if (idx >= 0 && (size_t)idx < m->n_peers) {
        m->unread[idx] = 0;
    }
}

ui_key_action ui_key_decide(int is_enter, int shift)
{
    if (!is_enter) {
        return UI_KEY_IGNORE;
    }
    return shift ? UI_KEY_NEWLINE : UI_KEY_SEND;
}

int ui_text_sendable(const char *text)
{
    const char *p = text;

    if (text == NULL || strlen(text) > UI_TEXT_BYTES) {
        return 0;
    }
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    return *p != '\0';
}
