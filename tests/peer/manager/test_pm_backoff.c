/* peer/manager : pair qui refuse toujours -> tentatives espacées (backoff exponentiel plafonné). */
#include "test.h"
#include "peer/manager/pm_test_util.h"

static char g_dir[64];
static loop_t *g_loop;
static sess_identity g_ida;
static util_rec_t g_ra;
static util_fake_t g_fake;

static void test_capped_backoff(void)
{
    pm_config cfg;
    pm_events ev = util_events();
    pm_t *a;
    uint64_t end;

    g_fake.close_at_accept = 1;
    util_cfg(&cfg, &g_ida, "alice");
    a = pm_new(g_loop, &cfg, &ev, &g_ra);
    CHECK(a != NULL);
    if (!a) {
        return;
    }
    CHECK_EQ_INT(pm_add_known(a, g_fake.id.pub, "fake", "127.0.0.1", tcp_listener_port(g_fake.ls)), 0);
    end = loop_now_ms(g_loop) + 1500;
    while (loop_now_ms(g_loop) < end) {
        (void)loop_run_once(g_loop, 5);
    }
    CHECK(g_fake.accepts >= 3);
    CHECK(g_fake.accepts <= 40);
    CHECK_EQ_INT(g_ra.connected_events, 0);
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
    RUN(test_capped_backoff);
    util_fake_stop(&g_fake);
    util_drain(g_loop, 50);
    loop_free(g_loop);
    return test_summary();
}
