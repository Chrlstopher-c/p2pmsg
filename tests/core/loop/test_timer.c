#define _GNU_SOURCE
#include "test.h"
#include "core/loop/loop.h"

#include <time.h>
#include <unistd.h>

#define REPEAT_TICKS 3

static uint64_t g_order[8];
static int g_order_n;
static uint64_t g_fired_at;
static uint64_t g_start_at;
static int g_ticks;
static uint64_t g_cancel_target;
static int g_cancel_hits;

static void rec_order(loop_t *l, uint64_t id, void *ud)
{
    (void)l;
    (void)ud;
    g_order[g_order_n++] = id;
}

static void stop_after_ms(loop_t *l, uint64_t id, void *ud)
{
    (void)id;
    (void)ud;
    loop_stop(l);
}

static void tick_repeat(loop_t *l, uint64_t id, void *ud)
{
    (void)id;
    (void)ud;
    if (++g_ticks == REPEAT_TICKS) {
        loop_stop(l);
    }
}

static void cancel_self(loop_t *l, uint64_t id, void *ud)
{
    (void)ud;
    g_cancel_hits++;
    loop_timer_cancel(l, id);
}

static void cancel_other(loop_t *l, uint64_t id, void *ud)
{
    (void)id;
    (void)ud;
    g_cancel_hits++;
    loop_timer_cancel(l, g_cancel_target);
}

static void record_fire(loop_t *l, uint64_t id, void *ud)
{
    (void)id;
    (void)ud;
    g_fired_at = loop_now_ms(l);
    loop_stop(l);
}

static void test_timer_order(void)
{
    loop_t *l = loop_new();

    CHECK(l != NULL);
    g_order_n = 0;
    CHECK(loop_timer_add(l, 30, 0, rec_order, NULL) > 0);
    CHECK(loop_timer_add(l, 10, 0, rec_order, NULL) > 0);
    CHECK(loop_timer_add(l, 20, 0, rec_order, NULL) > 0);
    CHECK(loop_timer_add(l, 60, 0, stop_after_ms, NULL) > 0);
    CHECK_EQ_INT(loop_run(l), 0);
    CHECK_EQ_INT(g_order_n, 3);
    CHECK_EQ_INT(g_order[0], 2);
    CHECK_EQ_INT(g_order[1], 3);
    CHECK_EQ_INT(g_order[2], 1);
    loop_free(l);
}

static void test_timer_repeat(void)
{
    loop_t *l = loop_new();
    uint64_t id;

    CHECK(l != NULL);
    g_ticks = 0;
    id = loop_timer_add(l, 5, 5, tick_repeat, NULL);
    CHECK(id > 0);
    CHECK_EQ_INT(loop_run(l), 0);
    CHECK_EQ_INT(g_ticks, REPEAT_TICKS);
    loop_timer_cancel(l, id);
    loop_free(l);
}

static void test_timer_cancel_from_outside(void)
{
    loop_t *l = loop_new();
    uint64_t id;

    CHECK(l != NULL);
    g_order_n = 0;
    id = loop_timer_add(l, 10, 0, rec_order, NULL);
    CHECK(id > 0);
    loop_timer_cancel(l, id);
    loop_timer_cancel(l, id);
    loop_timer_cancel(l, 999999);
    CHECK(loop_timer_add(l, 20, 0, stop_after_ms, NULL) > 0);
    CHECK_EQ_INT(loop_run(l), 0);
    CHECK_EQ_INT(g_order_n, 0);
    loop_free(l);
}

static void test_timer_cancel_self_in_callback(void)
{
    loop_t *l = loop_new();
    uint64_t id;

    CHECK(l != NULL);
    g_cancel_hits = 0;
    id = loop_timer_add(l, 2, 2, cancel_self, NULL);
    CHECK(id > 0);
    CHECK(loop_timer_add(l, 30, 0, stop_after_ms, NULL) > 0);
    CHECK_EQ_INT(loop_run(l), 0);
    CHECK_EQ_INT(g_cancel_hits, 1);
    loop_free(l);
}

static void test_timer_cancel_other_in_callback(void)
{
    loop_t *l = loop_new();
    uint64_t victim;

    CHECK(l != NULL);
    g_order_n = 0;
    g_cancel_hits = 0;
    victim = loop_timer_add(l, 50, 0, rec_order, NULL);
    g_cancel_target = victim;
    CHECK(loop_timer_add(l, 5, 0, cancel_other, NULL) > 0);
    CHECK(loop_timer_add(l, 100, 0, stop_after_ms, NULL) > 0);
    CHECK_EQ_INT(loop_run(l), 0);
    CHECK_EQ_INT(g_cancel_hits, 1);
    CHECK_EQ_INT(g_order_n, 0);
    loop_free(l);
}

static void test_timer_precision(void)
{
    loop_t *l = loop_new();
    struct timespec ts;
    uint64_t elapsed;

    CHECK(l != NULL);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    g_start_at = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
    CHECK(loop_timer_add(l, 100, 0, record_fire, NULL) > 0);
    CHECK_EQ_INT(loop_run(l), 0);
    elapsed = g_fired_at - g_start_at;
    CHECK(elapsed >= 80 && elapsed <= 120);
    loop_free(l);
}

static void test_timer_zero_delay_fires(void)
{
    loop_t *l = loop_new();

    CHECK(l != NULL);
    g_order_n = 0;
    CHECK(loop_timer_add(l, 0, 0, rec_order, NULL) > 0);
    CHECK(loop_run_once(l, 100) >= 0);
    CHECK_EQ_INT(g_order_n, 1);
    loop_free(l);
}

int main(void)
{
    RUN(test_timer_order);
    RUN(test_timer_repeat);
    RUN(test_timer_cancel_from_outside);
    RUN(test_timer_cancel_self_in_callback);
    RUN(test_timer_cancel_other_in_callback);
    RUN(test_timer_precision);
    RUN(test_timer_zero_delay_fires);
    return test_summary();
}
