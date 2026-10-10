#define _GNU_SOURCE
#include "test.h"
#include "core/loop/loop.h"

#include <pthread.h>

#define PRODUCERS 4
#define CALLS_PER_THREAD 1000

static loop_t *g_loop;
static int g_wakes;
static int g_done;

static void on_wake(loop_t *l, void *ud)
{
    (void)l;
    (void)ud;
    g_wakes++;
}

static void *producer(void *arg)
{
    int *bad = arg;

    for (int i = 0; i < CALLS_PER_THREAD; ++i) {
        if (loop_wake(g_loop) != 0) {
            (*bad)++;
        }
    }
    __atomic_fetch_add(&g_done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static void test_wake_from_no_callback_returns_error(void)
{
    loop_t *l = loop_new();

    CHECK(l != NULL);
    CHECK_EQ_INT(loop_wake(l), 0);
    loop_free(l);
}

static void test_wake_from_threads(void)
{
    pthread_t th[PRODUCERS];
    int bad[PRODUCERS] = {0};

    g_loop = loop_new();
    CHECK(g_loop != NULL);
    g_wakes = 0;
    g_done = 0;
    CHECK_EQ_INT(loop_set_wake(g_loop, on_wake, NULL), 0);
    for (int i = 0; i < PRODUCERS; ++i) {
        CHECK_EQ_INT(pthread_create(&th[i], NULL, producer, &bad[i]), 0);
    }
    while (__atomic_load_n(&g_done, __ATOMIC_SEQ_CST) < PRODUCERS) {
        CHECK(loop_run_once(g_loop, 10) >= 0);
    }
    for (int i = 0; i < PRODUCERS; ++i) {
        pthread_join(th[i], NULL);
    }
    CHECK_EQ_INT(loop_run_once(g_loop, 10) >= 0, 1);
    CHECK(g_wakes >= 1);
    for (int i = 0; i < PRODUCERS; ++i) {
        CHECK_EQ_INT(bad[i], 0);
    }
    loop_free(g_loop);
    g_loop = NULL;
}

int main(void)
{
    RUN(test_wake_from_no_callback_returns_error);
    RUN(test_wake_from_threads);
    return test_summary();
}
