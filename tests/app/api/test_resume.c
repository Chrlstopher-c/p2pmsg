#define _GNU_SOURCE
/* app/api : reprise des messages PENDING depuis une base pré-remplie. Deux apps dans le processus :
 * A porte un PENDING vers B (adresse via fichier peers), B doit recevoir exactement une fois. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app/api/app.h"
#include "app_test_util.h"
#include "sec/session/session.h"
#include "store/history/history.h"
#include "test.h"

#define WAIT_MS 10000
#define TEST_MSG_ID 0x1234u

typedef struct {
    pthread_mutex_t mu;
    int             got;
    uint64_t        id;
    char            text[64];
    app_msg_status  status;
} recv_t;

static recv_t g_b = {.mu = PTHREAD_MUTEX_INITIALIZER};

static void b_on_message(const app_message *m, void *ud)
{
    recv_t *r = ud;

    pthread_mutex_lock(&r->mu);
    r->got++;
    r->id = m->id;
    snprintf(r->text, sizeof r->text, "%s", m->text);
    pthread_mutex_unlock(&r->mu);
}

static int got_count(void)
{
    int n;

    pthread_mutex_lock(&g_b.mu);
    n = g_b.got;
    pthread_mutex_unlock(&g_b.mu);
    return n;
}

static void status_cb(const app_message *m, void *ud)
{
    app_msg_status *st = ud;

    if (m->outgoing) {
        *st = m->status;
    }
}

static void prefill_a(const char *dir, const char *b_hex, const uint8_t b_pub[32])
{
    char path[160];
    hist_t *h;
    hist_msg m;

    snprintf(path, sizeof path, "%s/history.db", dir);
    h = hist_open(path);
    CHECK(h != NULL);
    if (h == NULL) {
        return;
    }
    hist_upsert_peer(h, b_pub, "B", 1);
    memset(&m, 0, sizeof m);
    memcpy(m.peer, b_pub, 32);
    m.id = TEST_MSG_ID;
    m.dir = HIST_OUT;
    m.ts_ms = 1000;
    m.body = "hello";
    m.status = HIST_PENDING;
    CHECK_EQ_INT(hist_add(h, &m), 0);
    hist_close(h);
    (void)b_hex;
}

static void write_peers(const char *dir, uint16_t b_port, const char *b_hex)
{
    char path[160];
    FILE *f;

    snprintf(path, sizeof path, "%s/peers", dir);
    f = fopen(path, "w");
    CHECK(f != NULL);
    if (f != NULL) {
        fprintf(f, "127.0.0.1:%u %s B\n", (unsigned)b_port, b_hex);
        fclose(f);
    }
}

static void wait_delivered(app_t *a, const char *b_hex, app_msg_status *st)
{
    for (int t = 0; t < WAIT_MS; t += 100) {
        app_history(a, b_hex, UINT64_MAX, 10, status_cb, st);
        if (*st == APP_MSG_DELIVERED) {
            return;
        }
        test_sleep_ms(100);
    }
}

static void test_pending_resumed_once(void)
{
    char root_a[64], root_b[64], dir_a[96], dir_b[96], a_hex[65], b_hex[65];
    uint8_t b_pub[32];
    uint16_t pa = test_port(), pb = test_port();
    app_events ev_b = {.on_message = b_on_message};
    app_config cfg_b = {.dir = NULL, .name = "B", .listen_port = pb};
    app_config cfg_a = {.dir = NULL, .name = "A", .listen_port = pa};
    app_msg_status st = APP_MSG_PENDING;
    app_t *a = NULL, *b = NULL;

    CHECK_EQ_INT(test_tmpdir(root_a, sizeof root_a), 0);
    CHECK_EQ_INT(test_tmpdir(root_b, sizeof root_b), 0);
    snprintf(dir_a, sizeof dir_a, "%s/a", root_a);
    snprintf(dir_b, sizeof dir_b, "%s/b", root_b);
    CHECK_EQ_INT(mkdir(dir_a, 0700), 0);
    cfg_a.dir = dir_a;
    cfg_b.dir = dir_b;
    a = app_new(&cfg_a, NULL, NULL);
    CHECK(a != NULL);
    if (a == NULL) {
        goto out;
    }
    snprintf(a_hex, sizeof a_hex, "%s", app_self_key(a));
    app_free(a);
    a = NULL;
    CHECK_EQ_INT(mkdir(dir_b, 0700), 0);
    write_peers(dir_b, pa, a_hex);
    b = app_new(&cfg_b, &ev_b, &g_b);
    CHECK(b != NULL);
    if (b == NULL || app_start(b) != 0) {
        goto out;
    }
    snprintf(b_hex, sizeof b_hex, "%s", app_self_key(b));
    CHECK_EQ_INT(sess_key_from_hex(b_hex, b_pub), 0);
    prefill_a(dir_a, b_hex, b_pub);
    write_peers(dir_a, pb, b_hex);
    a = app_new(&cfg_a, NULL, NULL);
    CHECK(a != NULL);
    if (a == NULL || app_start(a) != 0) {
        goto out;
    }
    for (int t = 0; t < WAIT_MS && got_count() == 0; t += 100) {
        test_sleep_ms(100);
    }
    test_sleep_ms(500);
    CHECK_EQ_INT(got_count(), 1);
    pthread_mutex_lock(&g_b.mu);
    CHECK_EQ_INT(g_b.id, TEST_MSG_ID);
    CHECK(strcmp(g_b.text, "hello") == 0);
    pthread_mutex_unlock(&g_b.mu);
    wait_delivered(a, b_hex, &st);
    CHECK_EQ_INT(st, APP_MSG_DELIVERED);
out:
    app_free(a);
    app_free(b);
    test_rmdir_tree(dir_a);
    test_rmdir_tree(dir_b);
    rmdir(root_a);
    rmdir(root_b);
}

int main(void)
{
    RUN(test_pending_resumed_once);
    return test_summary();
}
