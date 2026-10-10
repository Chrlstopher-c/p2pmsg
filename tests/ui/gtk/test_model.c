/* ui/gtk : tests du modèle pur (sans display) — pairs, conversation, statuts, formatage, décisions de saisie. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "test.h"
#include "ui/gtk/ui_model.h"

#define KEY_A "aa00000000000000000000000000000000000000000000000000000000000001"
#define KEY_B "bb00000000000000000000000000000000000000000000000000000000000002"

static app_peer make_peer(const char *key, const char *name, app_peer_state st)
{
    app_peer p;

    memset(&p, 0, sizeof p);
    snprintf(p.key, sizeof p.key, "%s", key);
    snprintf(p.name, sizeof p.name, "%s", name);
    p.state = st;
    return p;
}

static app_message make_msg(const char *peer, uint64_t id, int outgoing, const char *text)
{
    app_message m;

    memset(&m, 0, sizeof m);
    snprintf(m.peer, sizeof m.peer, "%s", peer);
    m.id = id;
    m.outgoing = outgoing;
    m.ts_ms = 1000u * (uint64_t)id;
    m.text = text;
    m.status = outgoing ? APP_MSG_PENDING : APP_MSG_RECEIVED;
    return m;
}

static void test_upsert_peers(void)
{
    ui_model m;
    app_peer a = make_peer(KEY_A, "alice", APP_OFFLINE);
    app_peer a2 = make_peer(KEY_A, "alice", APP_CONNECTED);
    int is_new = 0;

    ui_model_init(&m);
    CHECK_EQ_INT(ui_model_upsert_peer(&m, &a, &is_new), 0);
    CHECK_EQ_INT(is_new, 1);
    CHECK_EQ_INT(ui_model_upsert_peer(&m, &a2, &is_new), 0);
    CHECK_EQ_INT(is_new, 0);
    CHECK_EQ_INT(m.n_peers, 1);
    CHECK_EQ_INT(m.peers[0].state, APP_CONNECTED);
    CHECK_EQ_INT(ui_model_find_peer(&m, KEY_B), -1);
}

static void test_peers_full(void)
{
    ui_model m;
    app_peer p;
    int is_new = 0;
    char key[APP_KEY_HEX];

    ui_model_init(&m);
    for (size_t i = 0; i < UI_PEERS_MAX; i++) {
        memset(&p, 0, sizeof p);
        snprintf(key, sizeof key, "%064zx", i);
        snprintf(p.key, sizeof p.key, "%s", key);
        CHECK_EQ_INT(ui_model_upsert_peer(&m, &p, &is_new) >= 0, 1);
    }
    memset(&p, 0, sizeof p);
    snprintf(p.key, sizeof p.key, "%s", KEY_B);
    CHECK_EQ_INT(ui_model_upsert_peer(&m, &p, &is_new), -1);
}

static void test_set_current_clears_conv(void)
{
    ui_model m;
    int evicted = 0;
    app_message msg = make_msg(KEY_A, 1, 0, "salut");

    ui_model_init(&m);
    CHECK_EQ_INT(ui_model_append(&m, &msg, &evicted) >= 0, 1);
    CHECK_EQ_INT(m.n_bubbles, 1);
    ui_model_set_current(&m, KEY_B);
    CHECK_EQ_INT(m.n_bubbles, 0);
    CHECK(strcmp(ui_model_current(&m), KEY_B) == 0);
    ui_model_set_current(&m, NULL);
    CHECK(ui_model_current(&m)[0] == '\0');
    ui_model_clear_conv(&m);
}

static void test_append_order_and_copy(void)
{
    ui_model m;
    char buf[32];
    int evicted = 1;
    app_message msg;

    ui_model_init(&m);
    snprintf(buf, sizeof buf, "%s", "premier");
    msg = make_msg(KEY_A, 7, 1, buf);
    CHECK_EQ_INT(ui_model_append(&m, &msg, &evicted), 0);
    CHECK_EQ_INT(evicted, 0);
    snprintf(buf, sizeof buf, "%s", "modifie");
    CHECK(strcmp(m.bubbles[0].text, "premier") == 0);
    CHECK_EQ_INT(m.bubbles[0].outgoing, 1);
    CHECK_EQ_INT(m.bubbles[0].status, APP_MSG_PENDING);
    ui_model_clear_conv(&m);
}

static void test_append_evicts_oldest(void)
{
    ui_model m;
    int evicted = 0;
    app_message msg;
    int last = -1;

    ui_model_init(&m);
    for (uint64_t i = 1; i <= UI_CONV_MAX + 1; i++) {
        msg = make_msg(KEY_A, i, 0, "x");
        last = ui_model_append(&m, &msg, &evicted);
    }
    CHECK_EQ_INT(m.n_bubbles, UI_CONV_MAX);
    CHECK_EQ_INT(evicted, 1);
    CHECK_EQ_INT(last, (int)UI_CONV_MAX - 1);
    CHECK_EQ_INT(m.bubbles[0].id, 2);
    CHECK_EQ_INT(ui_model_find_id(&m, 1), -1);
    ui_model_clear_conv(&m);
}

static void test_prepend_history_order(void)
{
    ui_model m;
    int evicted = 0;
    app_message newest = make_msg(KEY_A, 3, 0, "c");
    app_message mid = make_msg(KEY_A, 2, 1, "b");
    app_message oldest = make_msg(KEY_A, 1, 0, "a");
    app_message live = make_msg(KEY_A, 4, 0, "d");

    ui_model_init(&m);
    CHECK_EQ_INT(ui_model_prepend(&m, &newest), 0);
    CHECK_EQ_INT(ui_model_prepend(&m, &mid), 0);
    CHECK_EQ_INT(ui_model_prepend(&m, &oldest), 0);
    CHECK_EQ_INT(ui_model_append(&m, &live, &evicted), 3);
    CHECK_EQ_INT(m.bubbles[0].id, 1);
    CHECK_EQ_INT(m.bubbles[1].id, 2);
    CHECK_EQ_INT(m.bubbles[2].id, 3);
    CHECK_EQ_INT(m.bubbles[3].id, 4);
    CHECK(strcmp(m.bubbles[1].text, "b") == 0);
    ui_model_clear_conv(&m);
}

static void test_set_status(void)
{
    ui_model m;
    int evicted = 0;
    app_message out = make_msg(KEY_A, 9, 1, "ok");

    ui_model_init(&m);
    ui_model_set_current(&m, KEY_A);
    CHECK_EQ_INT(ui_model_append(&m, &out, &evicted), 0);
    CHECK_EQ_INT(ui_model_set_status(&m, KEY_A, 9, APP_MSG_DELIVERED), 0);
    CHECK_EQ_INT(m.bubbles[0].status, APP_MSG_DELIVERED);
    CHECK_EQ_INT(ui_model_set_status(&m, KEY_A, 99, APP_MSG_FAILED), -1);
    CHECK_EQ_INT(ui_model_set_status(&m, KEY_B, 9, APP_MSG_FAILED), -1);
    CHECK_EQ_INT(m.bubbles[0].status, APP_MSG_DELIVERED);
    ui_model_clear_conv(&m);
}

static void test_find_id_dedupe(void)
{
    ui_model m;
    int evicted = 0;
    app_message msg = make_msg(KEY_A, 42, 0, "hop");

    ui_model_init(&m);
    CHECK_EQ_INT(ui_model_find_id(&m, 42), -1);
    CHECK_EQ_INT(ui_model_append(&m, &msg, &evicted), 0);
    CHECK_EQ_INT(ui_model_find_id(&m, 42), 0);
    ui_model_clear_conv(&m);
}

static void test_time_format(void)
{
    char out[UI_TIME_LEN];

    setenv("TZ", "UTC", 1);
    tzset();
    CHECK_EQ_INT(ui_format_time(0, out, sizeof out), 0);
    CHECK(strcmp(out, "00:00") == 0);
    CHECK_EQ_INT(ui_format_time((uint64_t)(13 * 3600 + 7 * 60) * 1000u, out, sizeof out), 0);
    CHECK(strcmp(out, "13:07") == 0);
    CHECK_EQ_INT(ui_format_time(0, out, 3), -1);
}

static void test_status_glyph_and_classes(void)
{
    CHECK(strcmp(ui_status_glyph(APP_MSG_PENDING), "\xe2\x80\xa6") == 0);
    CHECK(strcmp(ui_status_glyph(APP_MSG_DELIVERED), "\xe2\x9c\x93") == 0);
    CHECK(strcmp(ui_status_glyph(APP_MSG_FAILED), "!") == 0);
    CHECK(strcmp(ui_status_glyph(APP_MSG_RECEIVED), "") == 0);
    CHECK(strcmp(ui_peer_css_class(APP_CONNECTED), "status-connected") == 0);
    CHECK(strcmp(ui_peer_css_class(APP_CONNECTING), "status-connecting") == 0);
    CHECK(strcmp(ui_peer_css_class(APP_OFFLINE), "status-offline") == 0);
}

static void test_unread_and_status_text(void)
{
    ui_model m;
    app_peer p = make_peer(KEY_A, "alice", APP_CONNECTED);
    int is_new = 0;

    ui_model_init(&m);
    CHECK_EQ_INT(ui_model_upsert_peer(&m, &p, &is_new), 0);
    ui_model_add_unread(&m, 0);
    ui_model_add_unread(&m, 0);
    CHECK_EQ_INT(m.unread[0], 2);
    ui_model_clear_unread(&m, 0);
    CHECK_EQ_INT(m.unread[0], 0);
    ui_model_add_unread(&m, 5);
    CHECK(strcmp(ui_peer_status_text(APP_CONNECTED), "connecté") == 0);
    CHECK(strcmp(ui_peer_status_text(APP_OFFLINE), "hors ligne") == 0);
}

static void test_key_decide(void)
{
    CHECK_EQ_INT(ui_key_decide(1, 0), UI_KEY_SEND);
    CHECK_EQ_INT(ui_key_decide(1, 1), UI_KEY_NEWLINE);
    CHECK_EQ_INT(ui_key_decide(0, 0), UI_KEY_IGNORE);
    CHECK_EQ_INT(ui_key_decide(0, 1), UI_KEY_IGNORE);
}

static void test_text_sendable(void)
{
    char big[UI_TEXT_BYTES + 2];

    CHECK_EQ_INT(ui_text_sendable(NULL), 0);
    CHECK_EQ_INT(ui_text_sendable(""), 0);
    CHECK_EQ_INT(ui_text_sendable(" \n\t "), 0);
    CHECK_EQ_INT(ui_text_sendable(" a "), 1);
    memset(big, 'x', UI_TEXT_BYTES);
    big[UI_TEXT_BYTES] = '\0';
    CHECK_EQ_INT(ui_text_sendable(big), 1);
    memset(big, 'x', UI_TEXT_BYTES + 1);
    big[UI_TEXT_BYTES + 1] = '\0';
    CHECK_EQ_INT(ui_text_sendable(big), 0);
}

int main(void)
{
    RUN(test_upsert_peers);
    RUN(test_peers_full);
    RUN(test_set_current_clears_conv);
    RUN(test_append_order_and_copy);
    RUN(test_append_evicts_oldest);
    RUN(test_prepend_history_order);
    RUN(test_set_status);
    RUN(test_find_id_dedupe);
    RUN(test_time_format);
    RUN(test_status_glyph_and_classes);
    RUN(test_unread_and_status_text);
    RUN(test_key_decide);
    RUN(test_text_sendable);
    return test_summary();
}
