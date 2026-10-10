/* Helpers partagés des tests app/api : dossiers temporaires, ports aléatoires, attente bornée. */
#ifndef P2P_TESTS_APP_API_UTIL_H
#define P2P_TESTS_APP_API_UTIL_H
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

static inline int test_tmpdir(char *out, size_t cap)
{
    snprintf(out, cap, "/tmp/p2p-app-XXXXXX");
    return mkdtemp(out) != NULL ? 0 : -1;
}

static inline void test_rmdir_tree(const char *dir)
{
    static const char *const NAMES[] = {"identity.key", "history.db", "history.db-wal",
                                        "history.db-shm", "peers", "p2pmsg.log"};
    char path[512];
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, NAMES[i]);
        unlink(path);
    }
    rmdir(dir);
}

static inline uint16_t test_port(void)
{
    uint32_t r = 0;
    if (getrandom(&r, sizeof r, 0) != (ssize_t)sizeof r) {
        r = (uint32_t)getpid();
    }
    return (uint16_t)(20000u + r % 30000u);
}

static inline void test_sleep_ms(long ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

#endif
