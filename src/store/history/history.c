#define _GNU_SOURCE
/* store/history : persistance SQLite (WAL, schéma v1, requêtes préparées à l'ouverture).
 * Conventions : u64 <-> int64 par memcpy (ids et ts). list/pending : pubkey lu depuis la ligne si absent.
 * hist_set_status : 0 mis à jour, 1 message absent, <0 erreur. */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>

#include "core/log/log.h"
#include "store/history/history.h"

#define HIST_MOD "store/history"
#define HIST_SCHEMA_VERSION 1
#define HIST_BUSY_MS 2000
#define HIST_PUB_LEN 32
#define HIST_FILE_MODE 0600

enum stmt_id {
    ST_PEER_UPSERT,
    ST_PEER_ENSURE,
    ST_PEER_ID,
    ST_MSG_INSERT,
    ST_MSG_STATUS,
    ST_LIST,
    ST_PENDING,
    ST_PEERS,
    ST_COUNT
};

static const char *const STMT_SQL[ST_COUNT] = {
    [ST_PEER_UPSERT] = "INSERT INTO peers(pubkey,name,last_seen) VALUES(?1,?2,?3) "
                       "ON CONFLICT(pubkey) DO UPDATE SET name=excluded.name, last_seen=excluded.last_seen",
    [ST_PEER_ENSURE] = "INSERT OR IGNORE INTO peers(pubkey,name,last_seen) VALUES(?1,'',0)",
    [ST_PEER_ID] = "SELECT id FROM peers WHERE pubkey=?1",
    [ST_MSG_INSERT] = "INSERT OR IGNORE INTO messages(id,peer_id,direction,ts,body,status) "
                      "VALUES(?1,?2,?3,?4,?5,?6)",
    [ST_MSG_STATUS] = "UPDATE messages SET status=?1 WHERE peer_id=(SELECT id FROM peers WHERE pubkey=?2) "
                      "AND id=?3 AND direction=?4",
    [ST_LIST] = "SELECT id,direction,ts,body,status FROM messages "
                "WHERE peer_id=(SELECT id FROM peers WHERE pubkey=?1) AND ts<?2 "
                "ORDER BY ts DESC, id DESC LIMIT ?3",
    [ST_PENDING] = "SELECT p.pubkey,m.id,m.direction,m.ts,m.body,m.status FROM messages m "
                   "JOIN peers p ON p.id=m.peer_id WHERE m.direction=0 AND m.status=0 "
                   "ORDER BY m.ts ASC, m.id ASC",
    [ST_PEERS] = "SELECT pubkey,name,last_seen FROM peers ORDER BY id",
};

static const char SCHEMA_V1[] =
    "CREATE TABLE peers(id INTEGER PRIMARY KEY, name TEXT, pubkey BLOB UNIQUE NOT NULL, last_seen INTEGER);"
    "CREATE TABLE messages(id INTEGER NOT NULL, peer_id INTEGER NOT NULL REFERENCES peers, direction INTEGER, "
    "ts INTEGER, body TEXT, status INTEGER, PRIMARY KEY(peer_id, id, direction));"
    "CREATE INDEX messages_peer_ts ON messages(peer_id, ts);"
    "PRAGMA user_version=1;";

static const char *const PRAGMAS[] = {
    "PRAGMA foreign_keys=ON",
    "PRAGMA journal_mode=WAL",
    "PRAGMA synchronous=NORMAL",
};

struct hist {
    sqlite3 *db;
    sqlite3_stmt *st[ST_COUNT];
};

static int db_error(sqlite3 *db, const char *op)
{
    LOG_E(HIST_MOD, "op=%s err=%s", op, sqlite3_errmsg(db));
    return -1;
}

static void db_log(sqlite3 *db, const char *op)
{
    LOG_E(HIST_MOD, "op=%s err=%s", op, sqlite3_errmsg(db));
}

static int64_t to_i64(uint64_t v)
{
    int64_t r;

    memcpy(&r, &v, sizeof r);
    return r;
}

static uint64_t to_u64(int64_t v)
{
    uint64_t r;

    memcpy(&r, &v, sizeof r);
    return r;
}

static int exec_sql(sqlite3 *db, const char *sql, const char *op)
{
    if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
        return db_error(db, op);
    }
    return 0;
}

static int touch_db_file(const char *path)
{
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, HIST_FILE_MODE);
    int err;

    if (fd < 0) {
        err = errno;
        LOG_E(HIST_MOD, "op=open-file path=%s err=%s", path, strerror(err));
        return -1;
    }
    if (fchmod(fd, HIST_FILE_MODE) != 0) {
        err = errno;
        LOG_E(HIST_MOD, "op=chmod path=%s err=%s", path, strerror(err));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int configure(sqlite3 *db)
{
    size_t i;

    if (sqlite3_busy_timeout(db, HIST_BUSY_MS) != SQLITE_OK) {
        return db_error(db, "busy-timeout");
    }
    for (i = 0; i < sizeof PRAGMAS / sizeof PRAGMAS[0]; ++i) {
        if (exec_sql(db, PRAGMAS[i], "pragma") != 0) {
            return -1;
        }
    }
    return 0;
}

static int read_user_version(sqlite3 *db, int *out)
{
    sqlite3_stmt *st = NULL;
    int rc;

    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &st, NULL) != SQLITE_OK) {
        return db_error(db, "user-version-prepare");
    }
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *out = sqlite3_column_int(st, 0);
    } else {
        db_log(db, "user-version-step");
    }
    sqlite3_finalize(st);
    return rc == SQLITE_ROW ? 0 : -1;
}

static int migrate_locked(sqlite3 *db)
{
    int ver = 0;

    if (read_user_version(db, &ver) != 0) {
        return -1;
    }
    if (ver > HIST_SCHEMA_VERSION) {
        LOG_E(HIST_MOD, "op=migrate err=schema version=%d > %d", ver, HIST_SCHEMA_VERSION);
        return -1;
    }
    if (ver == 0) {
        return exec_sql(db, SCHEMA_V1, "migrate-v1");
    }
    return 0;
}

static int migrate(sqlite3 *db)
{
    int rc;

    if (exec_sql(db, "BEGIN IMMEDIATE", "migrate-begin") != 0) {
        return -1;
    }
    rc = migrate_locked(db);
    if (rc != 0) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }
    return exec_sql(db, "COMMIT", "migrate-commit");
}

static int prepare_all(hist_t *h)
{
    int i;

    for (i = 0; i < ST_COUNT; ++i) {
        if (sqlite3_prepare_v2(h->db, STMT_SQL[i], -1, &h->st[i], NULL) != SQLITE_OK) {
            return db_error(h->db, "prepare");
        }
    }
    return 0;
}

void hist_close(hist_t *h)
{
    int i;

    if (h == NULL) {
        return;
    }
    for (i = 0; i < ST_COUNT; ++i) {
        sqlite3_finalize(h->st[i]);
    }
    sqlite3_close_v2(h->db);
    free(h);
}

hist_t *hist_open(const char *path)
{
    hist_t *h;

    if (path == NULL) {
        LOG_E(HIST_MOD, "op=open err=path NULL");
        return NULL;
    }
    if (touch_db_file(path) != 0) {
        return NULL;
    }
    h = calloc(1, sizeof *h);
    if (h == NULL) {
        LOG_E(HIST_MOD, "op=alloc err=out of memory");
        return NULL;
    }
    if (sqlite3_open_v2(path, &h->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        db_log(h->db, "open");
        hist_close(h);
        return NULL;
    }
    if (configure(h->db) != 0 || migrate(h->db) != 0 || prepare_all(h) != 0) {
        hist_close(h);
        return NULL;
    }
    return h;
}

static sqlite3_stmt *stmt_fresh(hist_t *h, enum stmt_id id)
{
    sqlite3_stmt *st = h->st[id];

    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    return st;
}

static int step_write(hist_t *h, sqlite3_stmt *st, const char *op)
{
    int rc = sqlite3_step(st);

    if (rc != SQLITE_DONE) {
        db_log(h->db, op);
    }
    sqlite3_reset(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int hist_upsert_peer(hist_t *h, const uint8_t pub[32], const char *name, uint64_t last_seen_ms)
{
    sqlite3_stmt *st = stmt_fresh(h, ST_PEER_UPSERT);
    int rc = sqlite3_bind_blob(st, 1, pub, HIST_PUB_LEN, SQLITE_TRANSIENT);

    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_text(st, 2, name != NULL ? name : "", -1, SQLITE_TRANSIENT);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int64(st, 3, to_i64(last_seen_ms));
    }
    if (rc != SQLITE_OK) {
        return db_error(h->db, "peer-upsert-bind");
    }
    return step_write(h, st, "peer-upsert");
}

static int ensure_peer(hist_t *h, const uint8_t pub[32])
{
    sqlite3_stmt *st = stmt_fresh(h, ST_PEER_ENSURE);

    if (sqlite3_bind_blob(st, 1, pub, HIST_PUB_LEN, SQLITE_TRANSIENT) != SQLITE_OK) {
        return db_error(h->db, "peer-ensure-bind");
    }
    return step_write(h, st, "peer-ensure");
}

static int peer_rowid(hist_t *h, const uint8_t pub[32], int64_t *out)
{
    sqlite3_stmt *st = stmt_fresh(h, ST_PEER_ID);
    int rc;

    if (sqlite3_bind_blob(st, 1, pub, HIST_PUB_LEN, SQLITE_TRANSIENT) != SQLITE_OK) {
        return db_error(h->db, "peer-id-bind");
    }
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *out = sqlite3_column_int64(st, 0);
    } else if (rc == SQLITE_DONE) {
        LOG_E(HIST_MOD, "op=peer-id err=peer absent after ensure");
    } else {
        db_log(h->db, "peer-id-step");
    }
    sqlite3_reset(st);
    return rc == SQLITE_ROW ? 0 : -1;
}

static int bind_msg(sqlite3_stmt *st, int64_t peer_rowid, const hist_msg *m)
{
    int rc = sqlite3_bind_int64(st, 1, to_i64(m->id));

    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int64(st, 2, peer_rowid);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(st, 3, (int)m->dir);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int64(st, 4, to_i64(m->ts_ms));
    }
    if (rc == SQLITE_OK) {
        rc = m->body != NULL ? sqlite3_bind_text(st, 5, m->body, -1, SQLITE_TRANSIENT)
                             : sqlite3_bind_null(st, 5);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(st, 6, (int)m->status);
    }
    return rc;
}

int hist_add(hist_t *h, const hist_msg *m)
{
    sqlite3_stmt *st;
    int64_t rowid = 0;

    if (ensure_peer(h, m->peer) != 0 || peer_rowid(h, m->peer, &rowid) != 0) {
        return -1;
    }
    st = stmt_fresh(h, ST_MSG_INSERT);
    if (bind_msg(st, rowid, m) != SQLITE_OK) {
        return db_error(h->db, "msg-insert-bind");
    }
    if (step_write(h, st, "msg-insert") != 0) {
        return -1;
    }
    return sqlite3_changes(h->db) == 0 ? 1 : 0;
}

int hist_set_status(hist_t *h, const uint8_t peer[32], uint64_t id, hist_dir dir, hist_status st_val)
{
    sqlite3_stmt *st = stmt_fresh(h, ST_MSG_STATUS);
    int rc = sqlite3_bind_int(st, 1, (int)st_val);

    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_blob(st, 2, peer, HIST_PUB_LEN, SQLITE_TRANSIENT);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int64(st, 3, to_i64(id));
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(st, 4, (int)dir);
    }
    if (rc != SQLITE_OK) {
        return db_error(h->db, "msg-status-bind");
    }
    if (step_write(h, st, "msg-status") != 0) {
        return -1;
    }
    return sqlite3_changes(h->db) == 0 ? 1 : 0;
}

static int fill_msg(sqlite3_stmt *st, const uint8_t *peer, hist_msg *m)
{
    int off = 0;

    if (peer != NULL) {
        memcpy(m->peer, peer, HIST_PUB_LEN);
    } else {
        if (sqlite3_column_bytes(st, 0) != HIST_PUB_LEN) {
            LOG_E(HIST_MOD, "op=row err=pubkey length");
            return -1;
        }
        memcpy(m->peer, sqlite3_column_blob(st, 0), HIST_PUB_LEN);
        off = 1;
    }
    m->id = to_u64(sqlite3_column_int64(st, off));
    m->dir = (hist_dir)sqlite3_column_int(st, off + 1);
    m->ts_ms = to_u64(sqlite3_column_int64(st, off + 2));
    m->body = (const char *)sqlite3_column_text(st, off + 3);
    m->status = (hist_status)sqlite3_column_int(st, off + 4);
    return 0;
}

static int next_row(hist_t *h, sqlite3_stmt *st, const char *op)
{
    int rc = sqlite3_step(st);

    if (rc == SQLITE_ROW) {
        return 1;
    }
    if (rc != SQLITE_DONE) {
        db_log(h->db, op);
        return -1;
    }
    return 0;
}

static int iter_msgs(hist_t *h, sqlite3_stmt *st, const uint8_t *peer, hist_msg_cb cb, void *ud,
                     const char *op)
{
    int rc;

    while ((rc = next_row(h, st, op)) > 0) {
        hist_msg m;

        rc = fill_msg(st, peer, &m);
        if (rc != 0 || cb(&m, ud) != 0) {
            break;
        }
    }
    sqlite3_reset(st);
    return rc < 0 ? -1 : 0;
}

int hist_list(hist_t *h, const uint8_t peer[32], uint64_t before_ms, int limit, hist_msg_cb cb, void *ud)
{
    sqlite3_stmt *st;
    int64_t bound = before_ms == UINT64_MAX ? INT64_MAX : to_i64(before_ms);
    int rc;

    if (limit <= 0) {
        return 0;
    }
    st = stmt_fresh(h, ST_LIST);
    rc = sqlite3_bind_blob(st, 1, peer, HIST_PUB_LEN, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int64(st, 2, bound);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(st, 3, limit);
    }
    if (rc != SQLITE_OK) {
        return db_error(h->db, "list-bind");
    }
    return iter_msgs(h, st, peer, cb, ud, "list");
}

int hist_pending(hist_t *h, hist_msg_cb cb, void *ud)
{
    return iter_msgs(h, stmt_fresh(h, ST_PENDING), NULL, cb, ud, "pending");
}

int hist_peers(hist_t *h, hist_peer_cb cb, void *ud)
{
    sqlite3_stmt *st = stmt_fresh(h, ST_PEERS);
    hist_peer p;
    int rc;

    while ((rc = next_row(h, st, "peers")) > 0) {
        if (sqlite3_column_bytes(st, 0) != HIST_PUB_LEN) {
            LOG_E(HIST_MOD, "op=peers err=pubkey length");
            rc = -1;
            break;
        }
        memcpy(p.pubkey, sqlite3_column_blob(st, 0), HIST_PUB_LEN);
        p.name = (const char *)sqlite3_column_text(st, 1);
        p.last_seen_ms = to_u64(sqlite3_column_int64(st, 2));
        if (cb(&p, ud) != 0) {
            break;
        }
    }
    sqlite3_reset(st);
    return rc < 0 ? -1 : 0;
}
