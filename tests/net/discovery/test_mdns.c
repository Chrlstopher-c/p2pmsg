/* Test mDNS reel : deux disc_t dans la meme loop, pk differentes, doivent se voir en < 5 s, et le retrait
 * (disc_free de l'un) doit produire on_lost chez l'autre. Sans avahi-daemon : SKIP (code 77). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "test.h"
#include "core/loop/loop.h"
#include "net/discovery/discovery.h"

#define SEEN_MS 5000u
#define LOST_MS 10000u
#define SKIP_CODE 77

typedef struct {
    uint8_t own[32];
    uint8_t other[32];
    int seen_other;
    int seen_self;
    int lost;
} probe_t;

static void on_peer(const disc_peer *p, void *ud)
{
    probe_t *pr = ud;

    if (memcmp(p->pubkey, pr->own, 32) == 0) {
        pr->seen_self++;
        return;
    }
    memcpy(pr->other, p->pubkey, 32);
    pr->seen_other++;
}

static void on_lost(const uint8_t pk[32], void *ud)
{
    probe_t *pr = ud;

    if (memcmp(pk, pr->other, 32) == 0) {
        pr->lost++;
    }
}

static int avahi_running(void)
{
    int rc = system("avahi-daemon --check >/dev/null 2>&1");

    return rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}

/* Fait tourner la loop jusqu'a ce que *counter > 0 ou que limit_ms soit ecoule. */
static int wait_for(loop_t *l, const int *counter, uint64_t limit_ms)
{
    uint64_t start = loop_now_ms(l);

    while (*counter <= 0) {
        if (loop_now_ms(l) - start > limit_ms) {
            return 0;
        }
        loop_run_once(l, 50);
    }
    return 1;
}

static disc_t *start_disc(loop_t *l, const char *name, uint8_t fill, uint16_t port, probe_t *pr)
{
    disc_handlers h = {on_peer, on_lost};
    disc_config cfg = {name, pr->own, port, NULL, 1};

    memset(pr->own, fill, sizeof pr->own);
    return disc_new(l, &cfg, &h, pr);
}

static void test_two_discs_see_each_other(void)
{
    loop_t *l = loop_new();
    probe_t pa = {{0}, {0}, 0, 0, 0};
    probe_t pb = {{0}, {0}, 0, 0, 0};
    char name_a[64];
    char name_b[64];
    disc_t *a;
    disc_t *b;

    snprintf(name_a, sizeof name_a, "p2ptest-%d-a", (int)getpid());
    snprintf(name_b, sizeof name_b, "p2ptest-%d-b", (int)getpid());
    a = start_disc(l, name_a, 0xAA, 47811, &pa);
    b = start_disc(l, name_b, 0xBB, 47812, &pb);
    CHECK(a != NULL);
    CHECK(b != NULL);
    CHECK(wait_for(l, &pa.seen_other, SEEN_MS));
    CHECK(wait_for(l, &pb.seen_other, SEEN_MS));
    CHECK_EQ_INT(pa.seen_self, 0);
    CHECK_EQ_INT(pb.seen_self, 0);
    disc_free(b);
    CHECK(wait_for(l, &pa.lost, LOST_MS));
    disc_free(a);
    loop_free(l);
}

int main(void)
{
    if (!avahi_running()) {
        fprintf(stdout, "SKIP: avahi-daemon indisponible (avahi-daemon --check)\n");
        return SKIP_CODE;
    }
    RUN(test_two_discs_see_each_other);
    return test_summary();
}
