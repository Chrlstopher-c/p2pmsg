#define _GNU_SOURCE
/* store/history : upsert pair, add idempotent, statuts, list/pending/peers, u64 limite, arrêt par callback. */
#include "history_test_util.h"

static void seed_msgs(hist_t *h, const uint8_t peer[32], const uint64_t *ts, int n)
{
    hist_msg m;

    for (int i = 0; i < n; ++i) {
        m = hist_mk(peer, (uint64_t)(i + 1), HIST_OUT, ts[i], "b", HIST_DELIVERED);
        CHECK_EQ_INT(hist_add(h, &m), 0);
    }
}

static void test_upsert_peer_updates_name(void)
{
    struct hist_fixture fx;
    struct peer_collect pc;
    uint8_t pk[32];
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pk, 1);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    CHECK_EQ_INT(hist_upsert_peer(h, pk, "alice", 10), 0);
    CHECK_EQ_INT(hist_upsert_peer(h, pk, "alicia", 20), 0);
    memset(&pc, 0, sizeof pc);
    CHECK_EQ_INT(hist_peers(h, collect_peer_cb, &pc), 0);
    CHECK_EQ_INT(pc.count, 1);
    CHECK(strcmp(pc.names[0], "alicia") == 0);
    hist_close(h);
    hist_fixture_drop(&fx);
}

static void test_add_unknown_peer_creates_row(void)
{
    struct hist_fixture fx;
    struct peer_collect pc;
    uint8_t pk[32];
    hist_msg m;
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pk, 2);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    m = hist_mk(pk, 1, HIST_IN, 5, "yo", HIST_RECEIVED);
    CHECK_EQ_INT(hist_add(h, &m), 0);
    memset(&pc, 0, sizeof pc);
    CHECK_EQ_INT(hist_peers(h, collect_peer_cb, &pc), 0);
    CHECK_EQ_INT(pc.count, 1);
    CHECK(pc.names[0][0] == '\0');
    CHECK_EQ_MEM(pc.keys[0], pk, 32);
    hist_close(h);
    hist_fixture_drop(&fx);
}

static void test_add_duplicate_returns_one(void)
{
    struct hist_fixture fx;
    struct msg_collect c;
    uint8_t pk[32];
    hist_msg m;
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pk, 3);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    m = hist_mk(pk, 9, HIST_OUT, 100, "first", HIST_PENDING);
    CHECK_EQ_INT(hist_add(h, &m), 0);
    m.body = "second";
    CHECK_EQ_INT(hist_add(h, &m), 1);
    memset(&c, 0, sizeof c);
    CHECK_EQ_INT(hist_list(h, pk, UINT64_MAX, 10, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 1);
    hist_close(h);
    hist_fixture_drop(&fx);
}

static void test_set_status(void)
{
    struct hist_fixture fx;
    struct msg_collect c;
    uint8_t pk[32];
    uint64_t ts[] = { 10 };
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pk, 4);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    hist_upsert_peer(h, pk, "p", 1);
    seed_msgs(h, pk, ts, 1);
    CHECK_EQ_INT(hist_set_status(h, pk, 1, HIST_OUT, HIST_FAILED), 0);
    CHECK_EQ_INT(hist_set_status(h, pk, 77, HIST_OUT, HIST_FAILED), 1);
    memset(&c, 0, sizeof c);
    CHECK_EQ_INT(hist_pending(h, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 0);
    hist_close(h);
    hist_fixture_drop(&fx);
}

static void test_list_order_and_pagination(void)
{
    struct hist_fixture fx;
    struct msg_collect c;
    uint8_t pk[32];
    uint64_t ts[] = { 10, 20, 30, 40, 50 };
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pk, 5);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    seed_msgs(h, pk, ts, 5);

    memset(&c, 0, sizeof c);
    CHECK_EQ_INT(hist_list(h, pk, UINT64_MAX, 10, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 5);
    CHECK_EQ_INT(c.ts[0], 50);
    CHECK_EQ_INT(c.ts[4], 10);

    memset(&c, 0, sizeof c);
    CHECK_EQ_INT(hist_list(h, pk, UINT64_MAX, 2, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 2);
    CHECK_EQ_INT(c.ts[0], 50);
    CHECK_EQ_INT(c.ts[1], 40);

    memset(&c, 0, sizeof c);
    CHECK_EQ_INT(hist_list(h, pk, 30, 10, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 2);
    CHECK_EQ_INT(c.ts[0], 20);
    CHECK_EQ_INT(c.ts[1], 10);
    hist_close(h);
    hist_fixture_drop(&fx);
}

static void test_list_isolates_peers(void)
{
    struct hist_fixture fx;
    struct msg_collect c;
    uint8_t pa[32], pb[32];
    uint64_t ts[] = { 1, 2 };
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pa, 6);
    hist_key(pb, 8);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    seed_msgs(h, pa, ts, 2);
    seed_msgs(h, pb, ts, 1);
    memset(&c, 0, sizeof c);
    CHECK_EQ_INT(hist_list(h, pb, UINT64_MAX, 10, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 1);
    hist_close(h);
    hist_fixture_drop(&fx);
}

static void test_pending_order_and_filter(void)
{
    struct hist_fixture fx;
    struct msg_collect c;
    uint8_t pk[32];
    hist_msg m;
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pk, 9);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    m = hist_mk(pk, 1, HIST_OUT, 30, "a", HIST_PENDING);
    CHECK_EQ_INT(hist_add(h, &m), 0);
    m = hist_mk(pk, 2, HIST_OUT, 10, "b", HIST_PENDING);
    CHECK_EQ_INT(hist_add(h, &m), 0);
    m = hist_mk(pk, 3, HIST_OUT, 20, "c", HIST_PENDING);
    CHECK_EQ_INT(hist_add(h, &m), 0);
    m = hist_mk(pk, 4, HIST_OUT, 15, "d", HIST_DELIVERED);
    CHECK_EQ_INT(hist_add(h, &m), 0);
    m = hist_mk(pk, 5, HIST_IN, 5, "e", HIST_PENDING);
    CHECK_EQ_INT(hist_add(h, &m), 0);
    memset(&c, 0, sizeof c);
    CHECK_EQ_INT(hist_pending(h, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 3);
    CHECK_EQ_INT(c.ids[0], 2);
    CHECK_EQ_INT(c.ids[1], 3);
    CHECK_EQ_INT(c.ids[2], 1);
    hist_close(h);
    hist_fixture_drop(&fx);
}

static void test_u64_max_roundtrip(void)
{
    struct hist_fixture fx;
    struct msg_collect c;
    uint8_t pk[32];
    hist_msg m;
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pk, 10);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    m = hist_mk(pk, UINT64_MAX, HIST_OUT, 1, "max", HIST_PENDING);
    CHECK_EQ_INT(hist_add(h, &m), 0);
    memset(&c, 0, sizeof c);
    CHECK_EQ_INT(hist_list(h, pk, UINT64_MAX, 1, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 1);
    CHECK(c.ids[0] == UINT64_MAX);
    CHECK_EQ_INT(hist_set_status(h, pk, UINT64_MAX, HIST_OUT, HIST_DELIVERED), 0);
    hist_close(h);
    hist_fixture_drop(&fx);
}

static void test_callback_stop(void)
{
    struct hist_fixture fx;
    struct msg_collect c;
    uint8_t pk[32];
    uint64_t ts[] = { 1, 2, 3, 4, 5 };
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pk, 11);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    seed_msgs(h, pk, ts, 5);
    memset(&c, 0, sizeof c);
    c.stop_at = 2;
    CHECK_EQ_INT(hist_list(h, pk, UINT64_MAX, 10, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 2);
    memset(&c, 0, sizeof c);
    c.stop_at = 1;
    CHECK_EQ_INT(hist_pending(h, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 0);
    hist_close(h);
    hist_fixture_drop(&fx);
}

static void test_open_bad_path_fails(void)
{
    hist_t *h = hist_open("/nonexistent-p2p-dir/x/history.db");

    CHECK(h == NULL);
}

int main(void)
{
    RUN(test_upsert_peer_updates_name);
    RUN(test_add_unknown_peer_creates_row);
    RUN(test_add_duplicate_returns_one);
    RUN(test_set_status);
    RUN(test_list_order_and_pagination);
    RUN(test_list_isolates_peers);
    RUN(test_pending_order_and_filter);
    RUN(test_u64_max_roundtrip);
    RUN(test_callback_stop);
    RUN(test_open_bad_path_fails);
    return test_summary();
}
