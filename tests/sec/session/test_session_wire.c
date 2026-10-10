#define _GNU_SOURCE
/* sec/session : trames hors protocole (DATA avant handshake, HS après), octet corrompu, timeout de handshake. */
#include "sess_harness.h"

static void world_listen(world_t *w)
{
    memset(w, 0, sizeof(*w));
    w->loop = loop_new();
    CHECK(w->loop != NULL);
    CHECK(harness_ids(w) == 0);
    w->ls = tcp_listen(w->loop, "127.0.0.1", 0, accept_cb, w);
    CHECK(w->ls != NULL);
    w->port = w->ls ? tcp_listener_port(w->ls) : 0;
    w->resp.id = &w->id_resp;
    w->resp.peer_expected = w->id_ini.pub;
}

static void test_data_before_handshake(void)
{
    world_t w;
    raw_t p;

    world_listen(&w);
    raw_connect(&w, &p, RAW_DATA_FIRST);
    harness_pump(&w, 300);
    CHECK_EQ_INT(w.resp.closes, 1);
    CHECK_EQ_INT(w.resp.close_err, SESS_EPROTO);
    CHECK_EQ_INT(w.resp.est, 0);
    CHECK_EQ_INT(w.resp.msgs, 0);
    harness_pump(&w, 100);
    raw_free(&p);
    harness_teardown(&w);
}

static void test_handshake_after_established(void)
{
    world_t w;
    raw_t p;
    static const uint8_t JUNK[4] = { 1, 2, 3, 4 };

    world_listen(&w);
    raw_connect(&w, &p, RAW_HS);
    harness_pump(&w, 300);
    CHECK(p.established);
    CHECK_EQ_INT(w.resp.est, 1);
    raw_send_frame(&p, SESS_FRAME_HS, JUNK, sizeof(JUNK));
    harness_pump(&w, 300);
    CHECK_EQ_INT(w.resp.closes, 1);
    CHECK_EQ_INT(w.resp.close_err, SESS_EPROTO);
    harness_pump(&w, 100);
    raw_free(&p);
    harness_teardown(&w);
}

static void test_corrupted_byte(void)
{
    world_t w;
    raw_t p;
    static const uint8_t PL[3] = { 'a', 'b', 'c' };

    world_listen(&w);
    raw_connect(&w, &p, RAW_HS);
    harness_pump(&w, 300);
    CHECK(p.established);
    CHECK_EQ_INT(raw_send_encrypted(&p, 0x20, PL, sizeof(PL), 0), 0);
    harness_pump(&w, 200);
    CHECK_EQ_INT(w.resp.msgs, 1);
    CHECK_EQ_INT(raw_send_encrypted(&p, 0x20, PL, sizeof(PL), 1), 0);
    harness_pump(&w, 300);
    CHECK_EQ_INT(w.resp.msgs, 1);
    CHECK_EQ_INT(w.resp.closes, 1);
    CHECK_EQ_INT(w.resp.close_err, SESS_EDECRYPT);
    harness_pump(&w, 100);
    raw_free(&p);
    harness_teardown(&w);
}

static void test_handshake_timeout(void)
{
    world_t w;
    raw_t p;
    uint64_t start;

    world_listen(&w);
    raw_connect(&w, &p, RAW_SILENT);
    start = loop_now_ms(w.loop);
    while (w.resp.closes == 0 && loop_now_ms(w.loop) < start + SESS_HS_TIMEOUT_MS + 2000u) {
        loop_run_once(w.loop, 20);
    }
    CHECK_EQ_INT(w.resp.closes, 1);
    CHECK_EQ_INT(w.resp.close_err, SESS_ETIMEOUT);
    CHECK(loop_now_ms(w.loop) - start >= SESS_HS_TIMEOUT_MS);
    harness_pump(&w, 100);
    raw_free(&p);
    harness_teardown(&w);
}

int main(void)
{
    RUN(test_data_before_handshake);
    RUN(test_handshake_after_established);
    RUN(test_corrupted_byte);
    RUN(test_handshake_timeout);
    return test_summary();
}
