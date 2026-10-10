#define _GNU_SOURCE
#include "test.h"
#include "core/loop/loop.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int g_pipe[2];
static int g_calls;
static uint32_t g_last_events;
static int g_other[2];
static int g_other_calls;

static void on_read_consume(loop_t *l, int fd, uint32_t ev, void *ud)
{
    char c;
    (void)ud;
    g_calls++;
    g_last_events = ev;
    if (read(fd, &c, 1) != 1) {
        return;
    }
    loop_stop(l);
}

static void on_read_del_self(loop_t *l, int fd, uint32_t ev, void *ud)
{
    char c;
    (void)ud;
    g_calls++;
    g_last_events = ev;
    CHECK_EQ_INT(read(fd, &c, 1), 1);
    CHECK_EQ_INT(loop_del_fd(l, fd), 0);
    CHECK_EQ_INT(loop_del_fd(l, fd), -ENOENT);
    loop_stop(l);
}

static void on_other_del(loop_t *l, int fd, uint32_t ev, void *ud)
{
    (void)fd;
    (void)ev;
    (void)ud;
    g_calls++;
    CHECK_EQ_INT(loop_del_fd(l, g_other[0]), 0);
    loop_stop(l);
}

static void on_other_count(loop_t *l, int fd, uint32_t ev, void *ud)
{
    char c;
    (void)l;
    (void)ev;
    (void)ud;
    ssize_t n;

    g_other_calls++;
    n = read(fd, &c, 1);
    CHECK_EQ_INT(n, 1);
}

static void make_pipe(int p[2])
{
    CHECK_EQ_INT(pipe2(p, O_NONBLOCK | O_CLOEXEC), 0);
}

static void test_add_and_dispatch_read(void)
{
    loop_t *l = loop_new();
    char c = 'x';

    CHECK(l != NULL);
    make_pipe(g_pipe);
    g_calls = 0;
    CHECK_EQ_INT(loop_add_fd(l, g_pipe[0], LOOP_READ, on_read_consume, NULL), 0);
    CHECK_EQ_INT(write(g_pipe[1], &c, 1), 1);
    CHECK_EQ_INT(loop_run_once(l, 1000) >= 1, 1);
    CHECK_EQ_INT(g_calls, 1);
    CHECK(g_last_events & LOOP_READ);
    CHECK_EQ_INT(loop_del_fd(l, g_pipe[0]), 0);
    loop_free(l);
    close(g_pipe[0]);
    close(g_pipe[1]);
}

static void test_add_invalid_args(void)
{
    loop_t *l = loop_new();

    CHECK(l != NULL);
    CHECK(loop_add_fd(l, -1, LOOP_READ, on_read_consume, NULL) < 0);
    CHECK_EQ_INT(loop_del_fd(l, 12345), -ENOENT);
    CHECK_EQ_INT(loop_mod_fd(l, 12345, LOOP_READ), -ENOENT);
    loop_free(l);
}

static void test_del_in_own_callback(void)
{
    loop_t *l = loop_new();
    char c = 'y';

    CHECK(l != NULL);
    make_pipe(g_pipe);
    g_calls = 0;
    CHECK_EQ_INT(loop_add_fd(l, g_pipe[0], LOOP_READ, on_read_del_self, NULL), 0);
    CHECK_EQ_INT(write(g_pipe[1], &c, 1), 1);
    CHECK_EQ_INT(loop_run(l), 0);
    CHECK_EQ_INT(g_calls, 1);
    CHECK_EQ_INT(loop_del_fd(l, g_pipe[0]), -ENOENT);
    loop_free(l);
    close(g_pipe[0]);
    close(g_pipe[1]);
}

static void test_del_other_fd_during_iteration(void)
{
    loop_t *l = loop_new();
    char c = 'z';

    CHECK(l != NULL);
    make_pipe(g_pipe);
    make_pipe(g_other);
    g_calls = 0;
    g_other_calls = 0;
    CHECK_EQ_INT(loop_add_fd(l, g_pipe[0], LOOP_READ, on_other_del, NULL), 0);
    CHECK_EQ_INT(loop_add_fd(l, g_other[0], LOOP_READ, on_other_count, NULL), 0);
    CHECK_EQ_INT(write(g_pipe[1], &c, 1), 1);
    CHECK_EQ_INT(write(g_other[1], &c, 1), 1);
    CHECK_EQ_INT(loop_run_once(l, 1000) >= 1, 1);
    CHECK_EQ_INT(g_calls, 1);
    CHECK_EQ_INT(g_other_calls, 0);
    loop_free(l);
    close(g_pipe[0]);
    close(g_pipe[1]);
    close(g_other[0]);
    close(g_other[1]);
}

static void test_mod_fd_changes_interest(void)
{
    loop_t *l = loop_new();
    char c = 'm';

    CHECK(l != NULL);
    make_pipe(g_pipe);
    g_calls = 0;
    CHECK_EQ_INT(loop_add_fd(l, g_pipe[0], LOOP_READ, on_read_consume, NULL), 0);
    CHECK_EQ_INT(loop_mod_fd(l, g_pipe[0], LOOP_WRITE), 0);
    CHECK_EQ_INT(write(g_pipe[1], &c, 1), 1);
    CHECK_EQ_INT(loop_run_once(l, 50), 0);
    CHECK_EQ_INT(g_calls, 0);
    CHECK_EQ_INT(loop_mod_fd(l, g_pipe[0], LOOP_READ), 0);
    CHECK_EQ_INT(loop_run_once(l, 1000) >= 1, 1);
    CHECK_EQ_INT(g_calls, 1);
    loop_free(l);
    close(g_pipe[0]);
    close(g_pipe[1]);
}

int main(void)
{
    RUN(test_add_and_dispatch_read);
    RUN(test_add_invalid_args);
    RUN(test_del_in_own_callback);
    RUN(test_del_other_fd_during_iteration);
    RUN(test_mod_fd_changes_interest);
    return test_summary();
}
