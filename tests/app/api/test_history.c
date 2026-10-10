#define _GNU_SOURCE
/* app/api : app_history sur une base pré-remplie (ordre, bornes, sens, statuts, pair inconnu). */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "app/api/app.h"
#include "app_test_util.h"
#include "store/history/history.h"
#include "sec/session/session.h"
#include "test.h"

#define MAX_SEEN 8

typedef struct {
    int      n;
    uint64_t ts[MAX_SEEN];
    int      outgoing[MAX_SEEN];
    int      status[MAX_SEEN];
} seen_t;

static void collect(const app_message *m, void *ud)
{
    seen_t *s = ud;

    if (s->n < MAX_SEEN) {
        s->ts[s->n] = m->ts_ms;
        s->outgoing[s->n] = m->outgoing;
        s->status[s->n] = (int)m->status;
    }
    s->n++;
}

static void seed(const char *dir, const uint8_t peer[32])
{
    char path[160];
    hist_t *h;
    static const struct { hist_dir dir; uint64_t ts; hist_status st; } ROWS[] = {
        {HIST_OUT, 100, HIST_DELIVERED}, {HIST_IN, 200, HIST_RECEIVED}, {HIST_OUT, 300, HIST_PENDING},
    };

    snprintf(path, sizeof path, "%s/history.db", dir);
    h = hist_open(path);
    if (h == NULL) {
        return;
    }
    hist_upsert_peer(h, peer, "p", 1);
    for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        hist_msg m;
        memset(&m, 0, sizeof m);
        memcpy(m.peer, peer, 32);
        m.id = i + 1;
        m.dir = ROWS[i].dir;
        m.ts_ms = ROWS[i].ts;
        m.body = "x";
        m.status = ROWS[i].st;
        hist_add(h, &m);
    }
    hist_close(h);
}

static void test_order_bounds_and_flags(void)
{
    char root[64], dir[96], peer_hex[65];
    uint8_t peer[32];
    seen_t s;
    app_config cfg;
    app_t *a;

    CHECK_EQ_INT(test_tmpdir(root, sizeof root), 0);
    snprintf(dir, sizeof dir, "%s/d", root);
    memset(peer, 0x5a, sizeof peer);
    sess_key_to_hex(peer, peer_hex);
    cfg = (app_config){.dir = dir, .name = "t", .listen_port = test_port()};
    CHECK_EQ_INT(mkdir(dir, 0700), 0);
    seed(dir, peer);
    a = app_new(&cfg, NULL, NULL);
    CHECK(a != NULL);
    if (a == NULL) {
        goto out;
    }
    memset(&s, 0, sizeof s);
    CHECK_EQ_INT(app_history(a, peer_hex, UINT64_MAX, 2, collect, &s), 0);
    CHECK_EQ_INT(s.n, 2);
    CHECK_EQ_INT(s.ts[0], 300);
    CHECK_EQ_INT(s.ts[1], 200);
    CHECK_EQ_INT(s.outgoing[0], 1);
    CHECK_EQ_INT(s.outgoing[1], 0);
    CHECK_EQ_INT(s.status[1], APP_MSG_RECEIVED);
    memset(&s, 0, sizeof s);
    CHECK_EQ_INT(app_history(a, peer_hex, 250, 10, collect, &s), 0);
    CHECK_EQ_INT(s.n, 2);
    CHECK_EQ_INT(s.ts[0], 200);
    CHECK_EQ_INT(s.status[1], APP_MSG_DELIVERED);
    CHECK_EQ_INT(app_history(a, peer_hex, UINT64_MAX, 0, collect, &s), -EINVAL);
    CHECK_EQ_INT(app_history(a, "xyz", UINT64_MAX, 5, collect, &s), -EINVAL);
    memset(peer, 0x01, sizeof peer);
    sess_key_to_hex(peer, peer_hex);
    CHECK_EQ_INT(app_history(a, peer_hex, UINT64_MAX, 5, collect, &s), 0);
    app_free(a);
out:
    test_rmdir_tree(dir);
    rmdir(root);
}

int main(void)
{
    RUN(test_order_bounds_and_flags);
    return test_summary();
}
