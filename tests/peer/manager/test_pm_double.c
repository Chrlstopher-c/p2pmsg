/* peer/manager : double connexion simultanée -> une seule session, celle initiée par la clé la plus petite. */
#include "test.h"
#include "peer/manager/pm_test_util.h"

static char g_dir[64];
static loop_t *g_loop;
static sess_identity g_ida;
static sess_identity g_idb;
static util_rec_t g_ra;
static util_rec_t g_rb;
static util_proxy_t g_p_ab;
static util_proxy_t g_p_ba;

static int cond_both_connected(void *ud)
{
    (void)ud;
    return g_ra.st == PM_CONNECTED && g_rb.st == PM_CONNECTED;
}

static int cond_one_live(void *ud)
{
    (void)ud;
    return g_p_ab.live + g_p_ba.live == 1;
}

static void tick_for(uint64_t ms)
{
    uint64_t end = loop_now_ms(g_loop) + ms;

    while (loop_now_ms(g_loop) < end) {
        (void)loop_run_once(g_loop, 5);
    }
}

static void test_single_session_survives(void)
{
    pm_config cfg;
    pm_events ev = util_events();
    pm_t *a;
    pm_t *b;
    int a_is_min = memcmp(g_ida.pub, g_idb.pub, 32) < 0;

    util_cfg(&cfg, &g_ida, "alice");
    a = pm_new(g_loop, &cfg, &ev, &g_ra);
    util_cfg(&cfg, &g_idb, "bob");
    b = pm_new(g_loop, &cfg, &ev, &g_rb);
    CHECK(a != NULL && b != NULL);
    if (!a || !b || util_px_start(&g_p_ab, g_loop, pm_listen_port(b)) != 0
        || util_px_start(&g_p_ba, g_loop, pm_listen_port(a)) != 0) {
        return;
    }
    CHECK_EQ_INT(pm_add_known(a, g_idb.pub, "bob", "127.0.0.1", util_px_port(&g_p_ab)), 0);
    CHECK_EQ_INT(pm_add_known(b, g_ida.pub, "alice", "127.0.0.1", util_px_port(&g_p_ba)), 0);
    CHECK(util_wait(g_loop, cond_both_connected, NULL, 5000));
    CHECK(util_wait(g_loop, cond_one_live, NULL, 5000));
    tick_for(500);
    CHECK_EQ_INT(g_p_ab.live + g_p_ba.live, 1);
    if (a_is_min) {
        CHECK_EQ_INT(g_p_ab.live, 1);
    } else {
        CHECK_EQ_INT(g_p_ba.live, 1);
    }
    CHECK_EQ_INT(pm_send(a, g_idb.pub, 1, 1, "ab"), 0);
    CHECK_EQ_INT(pm_send(b, g_ida.pub, 2, 2, "ba"), 0);
    tick_for(800);
    CHECK_EQ_INT(g_rb.msgs, 1);
    CHECK_EQ_INT(g_ra.msgs, 1);
    pm_free(a);
    util_drain(g_loop, 200);
    pm_free(b);
    util_drain(g_loop, 200);
}

int main(void)
{
    util_tmpdir(g_dir, sizeof g_dir);
    g_loop = loop_new();
    CHECK(g_loop != NULL);
    CHECK_EQ_INT(util_identity(&g_ida, g_dir, "alice"), 0);
    CHECK_EQ_INT(util_identity(&g_idb, g_dir, "bob"), 0);
    RUN(test_single_session_survives);
    util_px_stop(&g_p_ab);
    util_px_stop(&g_p_ba);
    util_drain(g_loop, 100);
    loop_free(g_loop);
    return test_summary();
}
