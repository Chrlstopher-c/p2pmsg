/* peer/manager, F4 : clé inattendue (sortant) et clé inconnue (entrant) rejetées, jamais CONNECTED. */
#include "test.h"
#include "peer/manager/pm_test_util.h"

static char g_dir[64];
static loop_t *g_loop;
static sess_identity g_ida;
static sess_identity g_wrong;
static util_rec_t g_ra;
static util_fake_t g_fake;

static const tcp_handlers NO_HANDLERS = { NULL, NULL, NULL };

static void tick_for(uint64_t ms)
{
    uint64_t end = loop_now_ms(g_loop) + ms;

    while (loop_now_ms(g_loop) < end) {
        (void)loop_run_once(g_loop, 5);
    }
}

static void test_outbound_wrong_key(void)
{
    pm_config cfg;
    pm_events ev = util_events();
    pm_t *a;

    util_cfg(&cfg, &g_ida, "alice");
    a = pm_new(g_loop, &cfg, &ev, &g_ra);
    CHECK(a != NULL);
    if (!a) {
        return;
    }
    /* Adresse du pair factice, mais clé attendue différente de la sienne. */
    CHECK_EQ_INT(pm_add_known(a, g_wrong.pub, "fake", "127.0.0.1", tcp_listener_port(g_fake.ls)), 0);
    tick_for(1000);
    CHECK(pm_peer_state(a, g_wrong.pub) != PM_CONNECTED);
    CHECK_EQ_INT(g_ra.connected_events, 0);
    CHECK_EQ_INT(g_fake.hellos, 0);
    pm_free(a);
    util_drain(g_loop, 200);
}

static void test_inbound_unknown_key(void)
{
    pm_config cfg;
    pm_events ev = util_events();
    util_rec_t rec;
    util_fake_t stranger;
    sess_handlers h;
    tcp_conn_t *c;
    pm_t *a;

    memset(&rec, 0, sizeof rec);
    memset(&stranger, 0, sizeof stranger);
    stranger.loop = g_loop;
    CHECK_EQ_INT(util_identity(&stranger.id, g_dir, "stranger"), 0);
    util_cfg(&cfg, &g_ida, "alice");
    a = pm_new(g_loop, &cfg, &ev, &rec);
    CHECK(a != NULL);
    if (!a) {
        return;
    }
    /* L'étranger ne figure dans aucune liste : pm doit refuser sa clé à l'entrée. */
    c = tcp_connect(g_loop, "127.0.0.1", pm_listen_port(a), &NO_HANDLERS, NULL);
    CHECK(c != NULL);
    if (c) {
        memset(&h, 0, sizeof h);
        h.on_verify = util_fake_verify;
        h.on_established = util_fake_on_established;
        h.on_message = util_fake_on_message;
        h.on_close = util_fake_on_close;
        CHECK(sess_new(g_loop, c, 0, SESS_INITIATOR, &stranger.id, &h, &stranger) != NULL);
    }
    tick_for(1000);
    CHECK_EQ_INT(rec.connected_events, 0);
    CHECK_EQ_INT(pm_peer_state(a, stranger.id.pub), PM_OFFLINE);
    CHECK(stranger.closes >= 1);
    CHECK_EQ_INT(stranger.hellos, 0);
    pm_free(a);
    util_drain(g_loop, 200);
}

int main(void)
{
    util_tmpdir(g_dir, sizeof g_dir);
    g_loop = loop_new();
    CHECK(g_loop != NULL);
    CHECK_EQ_INT(util_identity(&g_ida, g_dir, "alice"), 0);
    CHECK_EQ_INT(util_identity(&g_wrong, g_dir, "wrong"), 0);
    CHECK_EQ_INT(util_fake_start(&g_fake, g_loop, g_dir, "fake"), 0);
    RUN(test_outbound_wrong_key);
    RUN(test_inbound_unknown_key);
    util_fake_stop(&g_fake);
    util_drain(g_loop, 100);
    loop_free(g_loop);
    return test_summary();
}
