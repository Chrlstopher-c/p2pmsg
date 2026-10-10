#define _GNU_SOURCE
#include "test.h"
#include "core/loop/loop.h"

#include <fcntl.h>
#include <unistd.h>

static int g_fires;

static void on_fire(loop_t *l, uint64_t id, void *ud)
{
    (void)l;
    (void)id;
    (void)ud;
    g_fires++;
}

static void on_io(loop_t *l, int fd, uint32_t ev, void *ud)
{
    (void)l;
    (void)fd;
    (void)ev;
    (void)ud;
}

static void stop_now(loop_t *l, uint64_t id, void *ud)
{
    (void)id;
    (void)ud;
    loop_stop(l);
}

static void test_new_free_empty(void)
{
    loop_t *l = loop_new();

    CHECK(l != NULL);
    CHECK(loop_now_ms(l) > 0);
    loop_free(l);
    loop_free(NULL);
}

static void test_stop_ends_run(void)
{
    loop_t *l = loop_new();

    CHECK(l != NULL);
    CHECK(loop_timer_add(l, 5, 5, stop_now, NULL) > 0);
    CHECK_EQ_INT(loop_run(l), 0);
    loop_free(l);
}

static void test_free_with_fds_and_timers_remaining(void)
{
    loop_t *l = loop_new();
    int p[2];

    CHECK(l != NULL);
    CHECK_EQ_INT(pipe2(p, O_NONBLOCK | O_CLOEXEC), 0);
    g_fires = 0;
    CHECK_EQ_INT(loop_add_fd(l, p[0], LOOP_READ, on_io, NULL), 0);
    CHECK(loop_timer_add(l, 1000, 0, on_fire, NULL) > 0);
    CHECK(loop_timer_add(l, 1000, 500, on_fire, NULL) > 0);
    loop_free(l);
    CHECK_EQ_INT(g_fires, 0);
    close(p[0]);
    close(p[1]);
}

int main(void)
{
    RUN(test_new_free_empty);
    RUN(test_stop_ends_run);
    RUN(test_free_with_fds_and_timers_remaining);
    return test_summary();
}
