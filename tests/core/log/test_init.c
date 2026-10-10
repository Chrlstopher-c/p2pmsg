#define _GNU_SOURCE
#include "core/log/log_test_util.h"

static void test_init_returns_zero_and_truncates_new_file(void)
{
    char dir[FIX_CAP], path[FIX_CAP];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    log_shutdown();
    CHECK_EQ_INT(file_size(path), 0);
    fixture_drop(dir, path);
}

static void test_init_null_path_is_stderr_only(void)
{
    CHECK_EQ_INT(log_init(LOG_INFO, NULL), 0);
    log_shutdown();
}

static void test_init_bad_path_returns_minus_errno(void)
{
    CHECK_EQ_INT(log_init(LOG_INFO, "/nonexistent-p2p-dir/log.txt"), -ENOENT);
    log_shutdown();
}

static void test_init_truncates_existing_content(void)
{
    char dir[FIX_CAP], path[FIX_CAP];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(file_write(path, "OLD CONTENT\n"), 0);
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    log_shutdown();
    CHECK_EQ_INT(file_size(path), 0);
    fixture_drop(dir, path);
}

static void test_init_forces_mode_0600_on_existing_file(void)
{
    char dir[FIX_CAP], path[FIX_CAP];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(file_write(path, "x"), 0);
    CHECK_EQ_INT(chmod(path, 0644), 0);
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    log_shutdown();
    CHECK_EQ_INT(file_mode(path), 0600);
    fixture_drop(dir, path);
}

static void test_init_creates_new_file_with_mode_0600(void)
{
    char dir[FIX_CAP], path[FIX_CAP];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    log_shutdown();
    CHECK_EQ_INT(file_mode(path), 0600);
    fixture_drop(dir, path);
}

static void test_shutdown_is_idempotent_and_reinit_works(void)
{
    char dir[FIX_CAP], path[FIX_CAP];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    log_shutdown();
    log_shutdown();
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    LOG_I("init", "again");
    log_shutdown();
    CHECK(file_size(path) > 0);
    fixture_drop(dir, path);
}

static void test_reinit_truncates_previous_session(void)
{
    char dir[FIX_CAP], path[FIX_CAP];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    LOG_I("init", "session one");
    log_shutdown();
    CHECK(file_size(path) > 0);
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    log_shutdown();
    CHECK_EQ_INT(file_size(path), 0);
    fixture_drop(dir, path);
}

int main(void)
{
    RUN(test_init_returns_zero_and_truncates_new_file);
    RUN(test_init_null_path_is_stderr_only);
    RUN(test_init_bad_path_returns_minus_errno);
    RUN(test_init_truncates_existing_content);
    RUN(test_init_forces_mode_0600_on_existing_file);
    RUN(test_init_creates_new_file_with_mode_0600);
    RUN(test_shutdown_is_idempotent_and_reinit_works);
    RUN(test_reinit_truncates_previous_session);
    return test_summary();
}
