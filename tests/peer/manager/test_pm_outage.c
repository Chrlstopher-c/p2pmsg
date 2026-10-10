/* peer/manager, F5 : messages envoyés pendant une coupure, livrés une fois sans doublon après rétablissement. */
#include "test.h"
#include "peer/manager/pm_test_util.h"

static char g_dir[64];
static loop_t *g_loop;
static sess_identity g_ida;
static sess_identity g_idb;
static util_rec_t g_ra;
static util_rec_t g_rb;
static util_proxy_t g_px;

static int cond_a_connected(void *ud)
{
    (void)ud;
    return g_ra.st == PM_CONNECTED;
}

static int cond_a_down(void *ud)
{
    (void)ud;
    return g_ra.st != PM_CONNECTED;
}

static int cond_delivered(void *ud)
{
    (void)ud;
    return g_rb.msgs >= 3 && g_ra.delivered >= 3;
}

static void tick_for(uint64_t ms)
{
    uint64_t end = loop_now_ms(g_loop) + ms;

    while (loop_now_ms(g_loop) < end) {
        (void)loop_run_once(g_loop, 5);
    }
}

static void test_outage_no_loss_no_dup(void)
{
    pm_config cfg;
    pm_events ev;
    pm_t *a;
    pm_t *b;
    int i;

    ev = util_events();
    util_cfg(&cfg, &g_ida, "alice");
    a = pm_new(g_loop, &cfg, &ev, &g_ra);
    util_cfg(&cfg, &g_idb, "bob");
    b = pm_new(g_loop, &cfg, &ev, &g_rb);
    CHECK(a != NULL && b != NULL);
    if (!a || !b || util_px_start(&g_px, g_loop, pm_listen_port(b)) != 0) {
        return;
    }
    /* A dial B à travers le proxy ; B ne connaît pas l'adresse de A (entrant seulement). */
    CHECK_EQ_INT(pm_add_known(a, g_idb.pub, "bob", "127.0.0.1", util_px_port(&g_px)), 0);
    CHECK_EQ_INT(pm_add_known(b, g_ida.pub, "alice", NULL, 0), 0);
    CHECK(util_wait(g_loop, cond_a_connected, NULL, 5000));

    util_px_cut(&g_px, 1);
    CHECK(util_wait(g_loop, cond_a_down, NULL, 3000));
    for (i = 1; i <= 3; i++) {
        CHECK_EQ_INT(pm_send(a, g_idb.pub, (uint64_t)i, 1000u + (uint64_t)i, "coupure"), 0);
    }
    tick_for(500);
    CHECK_EQ_INT(g_rb.msgs, 0);

    util_px_cut(&g_px, 0);
    CHECK(util_wait(g_loop, cond_delivered, NULL, 10000));
    tick_for(500);
    CHECK_EQ_INT(g_rb.msgs, 3);
    CHECK_EQ_INT(g_ra.delivered, 3);
    CHECK_EQ_INT(g_rb.n_ids, 3);
    CHECK_EQ_INT(g_rb.ids[0], 1);
    CHECK_EQ_INT(g_rb.ids[1], 2);
    CHECK_EQ_INT(g_rb.ids[2], 3);
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
    RUN(test_outage_no_loss_no_dup);
    util_px_stop(&g_px);
    util_drain(g_loop, 100);
    loop_free(g_loop);
    return test_summary();
}
