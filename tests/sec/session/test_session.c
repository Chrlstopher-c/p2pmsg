#define _GNU_SOURCE
/* sec/session : handshake loopback, transport chiffré, rejet on_verify des deux côtés, on_close unique. */
#include "sess_harness.h"

static void world_start(world_t *w)
{
    memset(w, 0, sizeof(*w));
    w->loop = loop_new();
    CHECK(w->loop != NULL);
    CHECK(harness_ids(w) == 0);
}

static void test_handshake(void)
{
    world_t w;

    world_start(&w);
    harness_pair(&w);
    harness_pump(&w, 300);
    CHECK_EQ_INT(w.ini.est, 1);
    CHECK_EQ_INT(w.resp.est, 1);
    CHECK_EQ_INT(w.ini.verify_calls, 1);
    CHECK_EQ_INT(w.resp.verify_calls, 1);
    CHECK(w.ini.verify_key_ok);
    CHECK(w.resp.verify_key_ok);
    CHECK(w.ini.sess && sess_is_established(w.ini.sess));
    CHECK(w.resp.sess && sess_is_established(w.resp.sess));
    if (w.ini.sess) {
        CHECK_EQ_MEM(sess_peer_key(w.ini.sess), w.id_resp.pub, SESS_KEY_LEN);
    }
    harness_close_all(&w);
    harness_teardown(&w);
}

static void test_transport(void)
{
    world_t w;

    world_start(&w);
    harness_pair(&w);
    harness_pump(&w, 300);
    CHECK_EQ_INT(sess_send(w.ini.sess, 0x10, "hello", 5), 0);
    harness_pump(&w, 100);
    CHECK_EQ_INT(w.resp.msgs, 1);
    CHECK_EQ_INT(w.resp.last_type, 0x10);
    CHECK_EQ_INT(w.resp.last_len, 5);
    CHECK_EQ_MEM(w.resp.last_buf, "hello", 5);
    CHECK_EQ_INT(sess_send(w.resp.sess, 0x11, "world", 5), 0);
    harness_pump(&w, 100);
    CHECK_EQ_INT(w.ini.msgs, 1);
    CHECK_EQ_INT(w.ini.last_type, 0x11);
    CHECK_EQ_MEM(w.ini.last_buf, "world", 5);
    harness_close_all(&w);
    harness_teardown(&w);
}

static void test_reject_initiator(void)
{
    world_t w;

    world_start(&w);
    w.ini.verify_rc = -1;
    harness_pair(&w);
    harness_pump(&w, 300);
    CHECK_EQ_INT(w.ini.verify_calls, 1);
    CHECK_EQ_INT(w.ini.est, 0);
    CHECK_EQ_INT(w.ini.closes, 1);
    CHECK_EQ_INT(w.ini.close_err, SESS_EREJECTED);
    CHECK_EQ_INT(w.resp.est, 0);
    CHECK_EQ_INT(w.resp.msgs, 0);
    CHECK_EQ_INT(w.resp.closes, 1);
    harness_teardown(&w);
}

static void test_reject_responder(void)
{
    world_t w;

    world_start(&w);
    w.resp.verify_rc = -1;
    harness_pair(&w);
    harness_pump(&w, 300);
    CHECK_EQ_INT(w.resp.verify_calls, 1);
    CHECK_EQ_INT(w.resp.est, 0);
    CHECK_EQ_INT(w.resp.msgs, 0);
    CHECK_EQ_INT(w.resp.closes, 1);
    CHECK_EQ_INT(w.resp.close_err, SESS_EREJECTED);
    CHECK_EQ_INT(w.ini.closes, 1);
    harness_teardown(&w);
}

static void test_send_before_established(void)
{
    world_t w;

    world_start(&w);
    harness_pair(&w);
    CHECK_EQ_INT(sess_send(w.ini.sess, 0x01, "x", 1), -ENOTCONN);
    harness_close_all(&w);
    harness_teardown(&w);
}

static void test_send_size_limit(void)
{
    static uint8_t big[FRAME_MAX];
    world_t w;

    world_start(&w);
    harness_pair(&w);
    harness_pump(&w, 300);
    CHECK_EQ_INT(sess_send(w.ini.sess, 0x02, big, SESS_PLAINTEXT_MAX), 0);
    CHECK_EQ_INT(sess_send(w.ini.sess, 0x02, big, SESS_PLAINTEXT_MAX + 1u), -EMSGSIZE);
    harness_close_all(&w);
    harness_teardown(&w);
}

static void test_close_once(void)
{
    world_t w;

    world_start(&w);
    harness_pair(&w);
    harness_pump(&w, 300);
    sess_close(w.ini.sess, 0);
    harness_pump(&w, 200);
    CHECK_EQ_INT(w.ini.closes, 1);
    CHECK_EQ_INT(w.ini.close_err, 0);
    CHECK_EQ_INT(w.resp.closes, 1);
    CHECK_EQ_INT(w.resp.close_err, 0);
    harness_pump(&w, 100);
    CHECK_EQ_INT(w.ini.closes, 1);
    CHECK_EQ_INT(w.resp.closes, 1);
    harness_teardown(&w);
}

int main(void)
{
    RUN(test_handshake);
    RUN(test_transport);
    RUN(test_reject_initiator);
    RUN(test_reject_responder);
    RUN(test_send_before_established);
    RUN(test_send_size_limit);
    RUN(test_close_once);
    return test_summary();
}
