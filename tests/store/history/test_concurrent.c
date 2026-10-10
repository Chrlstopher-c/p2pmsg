#define _GNU_SOURCE
/* store/history : deux hist_t (un par thread) sur le même fichier, écritures concurrentes sans perte. */
#include <pthread.h>

#include "history_test_util.h"

#define PER_THREAD 500
#define OVERLAP 250

struct writer_arg {
    const char *path;
    const uint8_t *peer;
    uint64_t first_id;
    int ok;
    int dup;
    int err;
};

static void *writer(void *raw)
{
    struct writer_arg *a = raw;
    hist_t *h = hist_open(a->path);
    hist_msg m;
    int rc;

    if (h == NULL) {
        a->err = PER_THREAD;
        return NULL;
    }
    for (uint64_t i = 0; i < PER_THREAD; ++i) {
        m = hist_mk(a->peer, a->first_id + i, HIST_OUT, i + 1, "x", HIST_PENDING);
        rc = hist_add(h, &m);
        if (rc == 0) {
            a->ok++;
        } else if (rc == 1) {
            a->dup++;
        } else {
            a->err++;
        }
    }
    hist_close(h);
    return NULL;
}

static void test_two_handles_concurrent_writes(void)
{
    struct hist_fixture fx;
    struct writer_arg a = { 0 }, b = { 0 };
    struct msg_collect c;
    uint8_t pk[32];
    pthread_t ta, tb;
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pk, 42);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    hist_upsert_peer(h, pk, "conc", 1);
    hist_close(h);

    a.path = b.path = fx.path;
    a.peer = b.peer = pk;
    a.first_id = 1;
    b.first_id = 1 + OVERLAP;
    CHECK_EQ_INT(pthread_create(&ta, NULL, writer, &a), 0);
    CHECK_EQ_INT(pthread_create(&tb, NULL, writer, &b), 0);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);

    CHECK_EQ_INT(a.err + b.err, 0);
    CHECK_EQ_INT(a.ok + b.ok, PER_THREAD + OVERLAP);
    CHECK_EQ_INT(a.dup + b.dup, PER_THREAD - OVERLAP);

    h = hist_open(fx.path);
    CHECK(h != NULL);
    memset(&c, 0, sizeof c);
    CHECK_EQ_INT(hist_list(h, pk, UINT64_MAX, 1000, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, PER_THREAD + OVERLAP);
    hist_close(h);
    hist_fixture_drop(&fx);
}

int main(void)
{
    RUN(test_two_handles_concurrent_writes);
    return test_summary();
}
