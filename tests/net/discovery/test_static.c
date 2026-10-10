/* Tests disc_new en mode statique seul : emission au 1er tour (pas en synchrone), pk propre ignoree, dedoublonnage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "core/loop/loop.h"
#include "net/discovery/discovery.h"

#define PEERS_FILE "disc_static_test.txt"
#define K64_A "1111111111111111111111111111111111111111111111111111111111111111"
#define K64_B "2222222222222222222222222222222222222222222222222222222222222222"
#define K64_OWN "9999999999999999999999999999999999999999999999999999999999999999"

typedef struct {
    int count;
    int lost;
    uint8_t pks[4][32];
} probe_t;

static void on_peer(const disc_peer *p, void *ud)
{
    probe_t *pr = ud;

    if (pr->count < 4) {
        memcpy(pr->pks[pr->count], p->pubkey, 32);
    }
    pr->count++;
}

static void on_lost(const uint8_t pk[32], void *ud)
{
    probe_t *pr = ud;

    (void)pk;
    pr->lost++;
}

static void write_peers_file(void)
{
    FILE *f = fopen(PEERS_FILE, "w");

    if (!f) {
        return;
    }
    fprintf(f, "# liste de test\n");
    fprintf(f, "192.0.2.1:47800 %s peer1\n", K64_A);
    fprintf(f, "192.0.2.2:47800 %s peer2\r\n", K64_B);
    fprintf(f, "192.0.2.3:47800 %s self\n", K64_OWN);
    fprintf(f, "ligne invalide\n");
    fclose(f);
}

static void test_static_emitted_on_first_tick(void)
{
    loop_t *l = loop_new();
    probe_t pr = {0, 0, {{0}}};
    disc_handlers h = {on_peer, on_lost};
    uint8_t own[32];
    disc_config cfg = {"me", own, 47801, PEERS_FILE, 0};
    disc_t *d;

    memset(own, 0x99, sizeof own);
    write_peers_file();
    CHECK(l != NULL);
    d = disc_new(l, &cfg, &h, &pr);
    CHECK(d != NULL);
    CHECK_EQ_INT(pr.count, 0);
    loop_run_once(l, 0);
    CHECK_EQ_INT(pr.count, 2);
    CHECK_EQ_INT(pr.pks[0][0], 0x11);
    CHECK_EQ_INT(pr.pks[1][0], 0x22);
    loop_run_once(l, 0);
    CHECK_EQ_INT(pr.count, 2);
    disc_free(d);
    loop_free(l);
    remove(PEERS_FILE);
}

static void test_no_peers_file(void)
{
    loop_t *l = loop_new();
    probe_t pr = {0, 0, {{0}}};
    disc_handlers h = {on_peer, NULL};
    uint8_t own[32];
    disc_config cfg = {"me", own, 47801, NULL, 0};
    disc_t *d;

    memset(own, 0x99, sizeof own);
    d = disc_new(l, &cfg, &h, &pr);
    CHECK(d != NULL);
    loop_run_once(l, 0);
    CHECK_EQ_INT(pr.count, 0);
    disc_free(d);
    loop_free(l);
}

static void test_missing_peers_file_is_not_fatal(void)
{
    loop_t *l = loop_new();
    probe_t pr = {0, 0, {{0}}};
    disc_handlers h = {on_peer, NULL};
    uint8_t own[32];
    disc_config cfg = {"me", own, 47801, "/nonexistent/peers.txt", 0};
    disc_t *d;

    memset(own, 0x99, sizeof own);
    d = disc_new(l, &cfg, &h, &pr);
    CHECK(d != NULL);
    loop_run_once(l, 0);
    CHECK_EQ_INT(pr.count, 0);
    disc_free(d);
    loop_free(l);
}

static void test_invalid_config_returns_null(void)
{
    loop_t *l = loop_new();
    probe_t pr = {0, 0, {{0}}};
    disc_handlers h = {on_peer, NULL};
    disc_handlers no_peer = {NULL, NULL};
    uint8_t own[32];
    disc_config ok = {"me", own, 47801, NULL, 0};
    disc_config no_name = {NULL, own, 47801, NULL, 0};
    disc_config port0 = {"me", own, 0, NULL, 0};
    disc_config no_key = {"me", NULL, 47801, NULL, 0};

    memset(own, 0x99, sizeof own);
    CHECK(disc_new(NULL, &ok, &h, &pr) == NULL);
    CHECK(disc_new(l, NULL, &h, &pr) == NULL);
    CHECK(disc_new(l, &ok, NULL, &pr) == NULL);
    CHECK(disc_new(l, &ok, &no_peer, &pr) == NULL);
    CHECK(disc_new(l, &no_name, &h, &pr) == NULL);
    CHECK(disc_new(l, &port0, &h, &pr) == NULL);
    CHECK(disc_new(l, &no_key, &h, &pr) == NULL);
    loop_free(l);
}

int main(void)
{
    RUN(test_static_emitted_on_first_tick);
    RUN(test_no_peers_file);
    RUN(test_missing_peers_file_is_not_fatal);
    RUN(test_invalid_config_returns_null);
    return test_summary();
}
