/* peer/manager : keepalive -> PING sans trafic sortant ; pair muet (pas de PONG) -> fermeture puis reconnexion. */
#include "test.h"
#include "peer/manager/pm_test_util.h"

static char g_dir[64];
static loop_t *g_loop;
static sess_identity g_ida;
static util_rec_t g_ra;
static util_fake_t g_fake;

static int cond_connected(void *ud)
{
    (void)ud;
    return g_ra.st == PM_CONNECTED;
}

static int cond_pinged(void *ud)
{
    (void)ud;
    return g_fake.pings >= 1;
}

static int cond_dropped(void *ud)
{
    (void)ud;
    return g_ra.st != PM_CONNECTED && g_ra.connected_events >= 1;
}

static int cond_reconnected(void *ud)
{
    (void)ud;
    return g_ra.connected_events >= 2 && g_ra.st == PM_CONNECTED;
}

static void test_keepalive_and_dead_peer(void)
{
    pm_config cfg;
    pm_events ev = util_events();
    pm_t *a;

    g_fake.reply_pong = 0;
    util_cfg(&cfg, &g_ida, "alice");
    a = pm_new(g_loop, &cfg, &ev, &g_ra);
    CHECK(a != NULL);
    if (!a) {
        return;
    }
    CHECK_EQ_INT(pm_add_known(a, g_fake.id.pub, "fake", "127.0.0.1", tcp_listener_port(g_fake.ls)), 0);
    CHECK(util_wait(g_loop, cond_connected, NULL, 5000));
    CHECK(util_wait(g_loop, cond_pinged, NULL, 5000));
    CHECK(util_wait(g_loop, cond_dropped, NULL, 5000));
    CHECK(util_wait(g_loop, cond_reconnected, NULL, 10000));
    CHECK(g_fake.accepts >= 2);
    pm_free(a);
    util_drain(g_loop, 200);
}

int main(void)
{
    util_tmpdir(g_dir, sizeof g_dir);
    g_loop = loop_new();
    CHECK(g_loop != NULL);
    CHECK_EQ_INT(util_identity(&g_ida, g_dir, "alice"), 0);
    CHECK_EQ_INT(util_fake_start(&g_fake, g_loop, g_dir, "fake"), 0);
    RUN(test_keepalive_and_dead_peer);
    util_fake_stop(&g_fake);
    util_drain(g_loop, 50);
    loop_free(g_loop);
    return test_summary();
}
