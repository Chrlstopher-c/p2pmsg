#define _GNU_SOURCE
/* store/history : schéma, migration user_version, permissions 0600, persistance à la réouverture. */
#include <sqlite3.h>

#include "history_test_util.h"

static int query_first(const char *path, const char *sql, int *out_int, char *out_txt, size_t cap)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    int rc = -1;

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        return -1;
    }
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
        *out_int = sqlite3_column_int(st, 0);
        if (out_txt != NULL) {
            snprintf(out_txt, cap, "%s", (const char *)sqlite3_column_text(st, 0));
        }
        rc = 0;
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return rc;
}

static void test_schema_created(void)
{
    struct hist_fixture fx;
    hist_t *h;
    int n = -1;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    hist_close(h);
    CHECK_EQ_INT(query_first(fx.path, "SELECT count(*) FROM sqlite_master WHERE type='table' "
                 "AND name IN ('peers','messages')", &n, NULL, 0), 0);
    CHECK_EQ_INT(n, 2);
    n = -1;
    CHECK_EQ_INT(query_first(fx.path, "SELECT count(*) FROM sqlite_master WHERE type='index' "
                 "AND name='messages_peer_ts'", &n, NULL, 0), 0);
    CHECK_EQ_INT(n, 1);
    n = -1;
    CHECK_EQ_INT(query_first(fx.path, "PRAGMA user_version", &n, NULL, 0), 0);
    CHECK_EQ_INT(n, 1);
    hist_fixture_drop(&fx);
}

static void test_wal_mode(void)
{
    struct hist_fixture fx;
    hist_t *h;
    int n = -1;
    char mode[16] = "";

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    hist_close(h);
    CHECK_EQ_INT(query_first(fx.path, "PRAGMA journal_mode", &n, mode, sizeof mode), 0);
    CHECK(strcmp(mode, "wal") == 0);
    hist_fixture_drop(&fx);
}

static void test_file_mode_0600(void)
{
    struct hist_fixture fx;
    struct stat st;
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    hist_close(h);
    CHECK_EQ_INT(stat(fx.path, &st) == 0, 1);
    CHECK_EQ_INT(st.st_mode & 0777, 0600);
    hist_fixture_drop(&fx);
}

static void test_preexisting_file_chmod_0600(void)
{
    struct hist_fixture fx;
    struct stat st;
    hist_t *h;
    FILE *f;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    f = fopen(fx.path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fclose(f);
    }
    chmod(fx.path, 0644);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    hist_close(h);
    CHECK_EQ_INT(stat(fx.path, &st) == 0, 1);
    CHECK_EQ_INT(st.st_mode & 0777, 0600);
    hist_fixture_drop(&fx);
}

static void test_version_too_new_rejected(void)
{
    struct hist_fixture fx;
    sqlite3 *db = NULL;
    hist_t *h;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    CHECK_EQ_INT(sqlite3_open(fx.path, &db), SQLITE_OK);
    CHECK_EQ_INT(sqlite3_exec(db, "PRAGMA user_version=2", NULL, NULL, NULL), SQLITE_OK);
    sqlite3_close(db);
    h = hist_open(fx.path);
    CHECK(h == NULL);
    hist_fixture_drop(&fx);
}

static void test_reopen_persists(void)
{
    struct hist_fixture fx;
    uint8_t pk[32];
    hist_t *h;
    struct msg_collect c;
    hist_msg m;

    CHECK_EQ_INT(hist_fixture_make(&fx), 0);
    hist_key(pk, 7);
    h = hist_open(fx.path);
    CHECK(h != NULL);
    m = hist_mk(pk, 42, HIST_OUT, 1000, "salut", HIST_PENDING);
    CHECK_EQ_INT(hist_add(h, &m), 0);
    CHECK_EQ_INT(hist_set_status(h, pk, 42, HIST_OUT, HIST_DELIVERED), 0);
    hist_close(h);

    h = hist_open(fx.path);
    CHECK(h != NULL);
    memset(&c, 0, sizeof c);
    CHECK_EQ_INT(hist_list(h, pk, UINT64_MAX, 10, collect_msg_cb, &c), 0);
    CHECK_EQ_INT(c.count, 1);
    CHECK_EQ_INT(c.ids[0], 42);
    CHECK_EQ_INT(hist_add(h, &m), 1);
    hist_close(h);
    hist_fixture_drop(&fx);
}

int main(void)
{
    RUN(test_schema_created);
    RUN(test_wal_mode);
    RUN(test_file_mode_0600);
    RUN(test_preexisting_file_chmod_0600);
    RUN(test_version_too_new_rejected);
    RUN(test_reopen_persists);
    return test_summary();
}
