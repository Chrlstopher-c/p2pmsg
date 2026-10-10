/* peer/manager : ACK perdu -> renvoi -> doublon ignoré côté récepteur (un on_message, deux ACK). */
#include "test.h"
#include "peer/manager/pm_test_util.h"

static char g_dir[64];
static loop_t *g_loop;
static sess_identity g_ida;
static sess_identity g_idb;
static util_rec_t g_ra;
static util_rec_t g_rb;
static util_proxy_t g_px;

static int cond_connected(void *ud)
{
    (void)ud;
    return g_ra.st == PM_CONNECTED && g_rb.st == PM_CONNECTED;
}

static int cond_delivered(void *ud)
{
    (void)ud;
    return g_ra.delivered >= 1;
}

static void tick_for(uint64_t ms)
{
    uint64_t end = loop_now_ms(g_loop) + ms;

    while (loop_now_ms(g_loop) < end) {
        (void)loop_run_once(g_loop, 5);
    }
}

static void test_lost_ack_dedup(void)
{
    pm_config cfg;
    pm_events ev = util_events();
    pm_t *a;
    pm_t *b;
    int seen_before;

    util_cfg(&cfg, &g_ida, "alice");
    a = pm_new(g_loop, &cfg, &ev, &g_ra);
    util_cfg(&cfg, &g_idb, "bob");
    b = pm_new(g_loop, &cfg, &ev, &g_rb);
    CHECK(a != NULL && b != NULL);
    if (!a || !b || util_px_start(&g_px, g_loop, pm_listen_port(b)) != 0) {
        return;
    }
    CHECK_EQ_INT(pm_add_known(a, g_idb.pub, "bob", "127.0.0.1", util_px_port(&g_px)), 0);
    CHECK_EQ_INT(pm_add_known(b, g_ida.pub, "alice", NULL, 0), 0);
    CHECK(util_wait(g_loop, cond_connected, NULL, 5000));

    /* Le prochain trame de données B->A (l'ACK du message) est supprimée par le proxy. */
    seen_before = g_px.seen_b_data;
    g_px.drop_b = 1;
    CHECK_EQ_INT(pm_send(a, g_idb.pub, 9, 5000, "une seule fois"), 0);
    CHECK(util_wait(g_loop, cond_delivered, NULL, 5000));
    tick_for(300);
    CHECK_EQ_INT(g_rb.msgs, 1);
    CHECK(strcmp(g_rb.last_text, "une seule fois") == 0);
    CHECK_EQ_INT(g_ra.delivered, 1);
    CHECK(g_px.seen_b_data - seen_before >= 2);
    CHECK_EQ_INT(g_px.drop_b, 0);
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
    RUN(test_lost_ack_dedup);
    util_px_stop(&g_px);
    util_drain(g_loop, 100);
    loop_free(g_loop);
    return test_summary();
}
