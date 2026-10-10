/* peer/manager : pm_free avec un lien sortant et un lien entrant établis -> pairs notifiés, aucune fuite. */
#include "test.h"
#include "peer/manager/pm_test_util.h"

static char g_dir[64];
static loop_t *g_loop;
static sess_identity g_ida;
static sess_identity g_idb;
static sess_identity g_idc;
static util_rec_t g_ra;
static util_rec_t g_rb;
static util_rec_t g_rc;

static int cond_a_linked(void *ud)
{
    (void)ud;
    return g_ra.st == PM_CONNECTED && g_rb.st == PM_CONNECTED && g_rc.st == PM_CONNECTED;
}

static int cond_b_down(void *ud)
{
    (void)ud;
    return g_rb.st != PM_CONNECTED;
}

static int cond_c_down(void *ud)
{
    (void)ud;
    return g_rc.st != PM_CONNECTED;
}

static void test_free_with_both_directions(void)
{
    pm_config cfg;
    pm_events ev = util_events();
    pm_t *a;
    pm_t *b;
    pm_t *c;
    uint64_t t0;

    util_cfg(&cfg, &g_ida, "alice");
    a = pm_new(g_loop, &cfg, &ev, &g_ra);
    util_cfg(&cfg, &g_idb, "bob");
    b = pm_new(g_loop, &cfg, &ev, &g_rb);
    util_cfg(&cfg, &g_idc, "carol");
    c = pm_new(g_loop, &cfg, &ev, &g_rc);
    CHECK(a != NULL && b != NULL && c != NULL);
    if (!a || !b || !c) {
        return;
    }
    CHECK_EQ_INT(pm_add_known(a, g_idb.pub, "bob", "127.0.0.1", pm_listen_port(b)), 0);
    CHECK_EQ_INT(pm_add_known(a, g_idc.pub, "carol", NULL, 0), 0);
    CHECK_EQ_INT(pm_add_known(b, g_ida.pub, "alice", NULL, 0), 0);
    CHECK_EQ_INT(pm_add_known(c, g_ida.pub, "alice", "127.0.0.1", pm_listen_port(a)), 0);
    CHECK(util_wait(g_loop, cond_a_linked, NULL, 5000));
    t0 = loop_now_ms(g_loop);
    pm_free(a);
    CHECK(loop_now_ms(g_loop) - t0 <= 1000);
    CHECK(util_wait(g_loop, cond_b_down, NULL, 3000));
    CHECK(util_wait(g_loop, cond_c_down, NULL, 3000));
    pm_free(b);
    pm_free(c);
    util_drain(g_loop, 200);
}

int main(void)
{
    util_tmpdir(g_dir, sizeof g_dir);
    g_loop = loop_new();
    CHECK(g_loop != NULL);
    CHECK_EQ_INT(util_identity(&g_ida, g_dir, "alice"), 0);
    CHECK_EQ_INT(util_identity(&g_idb, g_dir, "bob"), 0);
    CHECK_EQ_INT(util_identity(&g_idc, g_dir, "carol"), 0);
    RUN(test_free_with_both_directions);
    loop_free(g_loop);
    return test_summary();
}
