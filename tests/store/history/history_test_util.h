/* Helpers partagés des tests store/history : fixtures SQLite temporaires, collecteurs de callbacks. */
#ifndef P2P_TESTS_STORE_HISTORY_UTIL_H
#define P2P_TESTS_STORE_HISTORY_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "store/history/history.h"
#include "test.h"

#define HIST_FIX_CAP 256
#define HIST_MAX_COLLECT 64

struct hist_fixture {
    char dir[HIST_FIX_CAP];
    char path[HIST_FIX_CAP * 2];
};

static inline int hist_fixture_make(struct hist_fixture *fx)
{
    snprintf(fx->dir, sizeof fx->dir, "/tmp/p2p-hist-test-XXXXXX");
    if (mkdtemp(fx->dir) == NULL) {
        return -1;
    }
    snprintf(fx->path, sizeof fx->path, "%s/history.db", fx->dir);
    return 0;
}

static inline void hist_fixture_drop(const struct hist_fixture *fx)
{
    char extra[HIST_FIX_CAP * 2 + 8];
    static const char *const suffixes[] = { "", "-wal", "-shm" };

    for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; ++i) {
        snprintf(extra, sizeof extra, "%s%s", fx->path, suffixes[i]);
        unlink(extra);
    }
    rmdir(fx->dir);
}

static inline void hist_key(uint8_t key[32], uint8_t seed)
{
    memset(key, seed, 32);
}

static inline hist_msg hist_mk(const uint8_t peer[32], uint64_t id, hist_dir dir, uint64_t ts,
                               const char *body, hist_status st)
{
    hist_msg m;

    memcpy(m.peer, peer, 32);
    m.id = id;
    m.dir = dir;
    m.ts_ms = ts;
    m.body = body;
    m.status = st;
    return m;
}

struct msg_collect {
    int count;
    int stop_at;
    uint64_t ids[HIST_MAX_COLLECT];
    uint64_t ts[HIST_MAX_COLLECT];
};

static inline int collect_msg_cb(const hist_msg *m, void *ud)
{
    struct msg_collect *c = ud;

    if (c->count < HIST_MAX_COLLECT) {
        c->ids[c->count] = m->id;
        c->ts[c->count] = m->ts_ms;
    }
    c->count++;
    return (c->stop_at > 0 && c->count >= c->stop_at) ? 1 : 0;
}

struct peer_collect {
    int count;
    uint8_t keys[HIST_MAX_COLLECT][32];
    char names[HIST_MAX_COLLECT][64];
};

static inline int collect_peer_cb(const hist_peer *p, void *ud)
{
    struct peer_collect *c = ud;

    if (c->count < HIST_MAX_COLLECT) {
        memcpy(c->keys[c->count], p->pubkey, 32);
        snprintf(c->names[c->count], sizeof c->names[0], "%s", p->name);
    }
    c->count++;
    return 0;
}

#endif
