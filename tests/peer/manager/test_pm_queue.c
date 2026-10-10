/* peer/manager : pair inconnu -> -ENOENT ; file pleine -> -ENOSPC (pas de perte silencieuse). */
#include "test.h"
#include "peer/manager/pm_test_util.h"

static char g_dir[64];
static loop_t *g_loop;
static sess_identity g_ida;
static sess_identity g_idx;
static util_rec_t g_ra;
static util_fake_t g_fake;

static void test_unknown_peer_enoent(void)
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
    CHECK_EQ_INT(pm_send(a, g_idx.pub, 1, 1, "inconnu"), -ENOENT);
    pm_free(a);
    util_drain(g_loop, 200);
}

static void test_full_queue_enospc(void)
{
    pm_config cfg;
    pm_events ev = util_events();
    pm_t *a;
    uint32_t i;
    int rc = 0;

    util_cfg(&cfg, &g_ida, "alice");
    a = pm_new(g_loop, &cfg, &ev, &g_ra);
    CHECK(a != NULL);
    if (!a) {
        return;
    }
    CHECK_EQ_INT(pm_add_known(a, g_fake.id.pub, "fake", "127.0.0.1", 9), 0);
    for (i = 0; i < PM_QUEUE_MAX; i++) {
        rc = pm_send(a, g_fake.id.pub, (uint64_t)i + 1, 1, "x");
        if (rc != 0) {
            break;
        }
    }
    CHECK_EQ_INT(rc, 0);
    CHECK_EQ_INT(pm_send(a, g_fake.id.pub, 999999u, 1, "overflow"), -ENOSPC);
    pm_free(a);
    util_drain(g_loop, 200);
}

int main(void)
{
    util_tmpdir(g_dir, sizeof g_dir);
    g_loop = loop_new();
    CHECK(g_loop != NULL);
    CHECK_EQ_INT(util_identity(&g_ida, g_dir, "alice"), 0);
    CHECK_EQ_INT(util_identity(&g_idx, g_dir, "stranger"), 0);
    CHECK_EQ_INT(util_identity(&g_fake.id, g_dir, "fake"), 0);
    RUN(test_unknown_peer_enoent);
    RUN(test_full_queue_enospc);
    util_fake_stop(&g_fake);
    util_drain(g_loop, 100);
    loop_free(g_loop);
    return test_summary();
}
