#define _GNU_SOURCE
/* sec/session : rekey Noise après SESS_REKEY_EVERY messages par sens (25000 messages, deux rekeys). */
#include "sess_harness.h"

#define REKEY_MSGS_FWD  25000u
#define REKEY_MSGS_BACK 10001u

static void pump_send(world_t *w, sess_t *from, uint32_t seq)
{
    uint8_t pl[4];
    int rc;

    buf_store_u32be(pl, seq);
    do {
        rc = sess_send(from, 0x30, pl, sizeof(pl));
        if (rc == TCP_ETXFULL) {
            loop_run_once(w->loop, 0);
        }
    } while (rc == TCP_ETXFULL);
    CHECK_EQ_INT(rc, 0);
    loop_run_once(w->loop, 0);
}

static void test_rekey_both_directions(void)
{
    world_t w;
    uint32_t i;

    memset(&w, 0, sizeof(w));
    w.loop = loop_new();
    CHECK(w.loop != NULL);
    CHECK(harness_ids(&w) == 0);
    harness_pair(&w);
    harness_pump(&w, 300);
    CHECK(w.ini.sess && sess_is_established(w.ini.sess));
    for (i = 0; i < REKEY_MSGS_FWD; ++i) {
        pump_send(&w, w.ini.sess, i);
    }
    harness_pump(&w, 300);
    CHECK_EQ_INT(w.resp.msgs, (int)REKEY_MSGS_FWD);
    CHECK_EQ_INT(w.resp.closes, 0);
    CHECK_EQ_INT(buf_load_u32be(w.resp.last_buf), REKEY_MSGS_FWD - 1u);
    for (i = 0; i < REKEY_MSGS_BACK; ++i) {
        pump_send(&w, w.resp.sess, i);
    }
    harness_pump(&w, 300);
    CHECK_EQ_INT(w.ini.msgs, (int)REKEY_MSGS_BACK);
    CHECK_EQ_INT(w.ini.closes, 0);
    CHECK_EQ_INT(w.resp.closes, 0);
    harness_close_all(&w);
    harness_teardown(&w);
}

int main(void)
{
    RUN(test_rekey_both_directions);
    return test_summary();
}
