/* peer/manager : les logs ne contiennent jamais le texte d'un message (marqueur unique cherché). */
#include "test.h"
#include "core/log/log.h"
#include "peer/manager/pm_test_util.h"

#define LOG_MARKER "ZQX-SECRET-7731"
#define LOG_BUF 65536

static char g_dir[64];
static char g_logpath[PATH_MAX];
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

static size_t read_log(char *buf, size_t cap)
{
    FILE *f = fopen(g_logpath, "rb");
    size_t n;

    if (!f) {
        return 0;
    }
    n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    (void)fclose(f);
    return n;
}

static void test_logs_have_no_message_text(void)
{
    pm_config cfg;
    pm_events ev = util_events();
    pm_t *a;
    pm_t *b;
    static char buf[LOG_BUF];
    size_t n;

    CHECK_EQ_INT(log_init(LOG_DEBUG, g_logpath), 0);
    LOG_I("test", "journal actif");
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
    CHECK_EQ_INT(pm_send(a, g_idb.pub, 42, 1, LOG_MARKER " contenu secret"), 0);
    CHECK(util_wait(g_loop, cond_delivered, NULL, 5000));
    CHECK(strstr(g_rb.last_text, LOG_MARKER) != NULL);
    pm_free(a);
    util_drain(g_loop, 200);
    pm_free(b);
    util_drain(g_loop, 200);
    util_px_stop(&g_px);
    util_drain(g_loop, 100);
    log_shutdown();
    n = read_log(buf, sizeof buf);
    CHECK(n > 0);
    CHECK(strstr(buf, LOG_MARKER) == NULL);
}

int main(void)
{
    util_tmpdir(g_dir, sizeof g_dir);
    snprintf(g_logpath, sizeof g_logpath, "%s/pm.log", g_dir);
    g_loop = loop_new();
    CHECK(g_loop != NULL);
    CHECK_EQ_INT(util_identity(&g_ida, g_dir, "alice"), 0);
    CHECK_EQ_INT(util_identity(&g_idb, g_dir, "bob"), 0);
    RUN(test_logs_have_no_message_text);
    loop_free(g_loop);
    return test_summary();
}
