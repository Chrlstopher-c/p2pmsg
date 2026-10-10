#define _GNU_SOURCE
#include "core/log/log_test_util.h"

#include <time.h>

#define BUF_CAP 4096

static void test_pre_init_writes_info_to_stderr(void)
{
    char dir[FIX_CAP], path[FIX_CAP], buf[BUF_CAP];
    int saved, fd;

    FIXTURE_OR_RETURN(dir, path);
    log_shutdown();
    saved = dup(STDERR_FILENO);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    dup2(fd, STDERR_FILENO);
    close(fd);
    LOG_D("pre", "hidden");
    LOG_I("pre", "shown %d", 1);
    dup2(saved, STDERR_FILENO);
    close(saved);
    file_read(path, buf, sizeof buf);
    CHECK_EQ_INT(count_char(buf, '\n'), 1);
    CHECK(rx_matches(LINE_RE "pre: shown 1\n$", buf));
    fixture_drop(dir, path);
}

static void test_line_format_matches_regex(void)
{
    char dir[FIX_CAP], path[FIX_CAP], buf[BUF_CAP];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(log_init(LOG_DEBUG, path), 0);
    LOG_I("core", "hello %d key=%s", 42, "v");
    log_shutdown();
    file_read(path, buf, sizeof buf);
    CHECK(rx_matches(LINE_RE "core: hello 42 key=v\n$", buf));
    CHECK_EQ_INT(count_char(buf, '\n'), 1);
    fixture_drop(dir, path);
}

static void test_all_levels_emitted_at_debug_min(void)
{
    char dir[FIX_CAP], path[FIX_CAP], buf[BUF_CAP];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(log_init(LOG_DEBUG, path), 0);
    LOG_D("dbg", "a");
    LOG_I("inf", "b");
    LOG_W("wrn", "c");
    LOG_E("err", "d");
    log_shutdown();
    file_read(path, buf, sizeof buf);
    CHECK_EQ_INT(count_char(buf, '\n'), 4);
    CHECK(strstr(buf, " DEBUG dbg: a\n") != NULL);
    CHECK(strstr(buf, " INFO inf: b\n") != NULL);
    CHECK(strstr(buf, " WARN wrn: c\n") != NULL);
    CHECK(strstr(buf, " ERROR err: d\n") != NULL);
    fixture_drop(dir, path);
}

static void test_min_level_filters_lower_levels(void)
{
    char dir[FIX_CAP], path[FIX_CAP], buf[BUF_CAP];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(log_init(LOG_WARN, path), 0);
    LOG_D("flt", "dropped-d");
    LOG_I("flt", "dropped-i");
    LOG_W("flt", "kept-w");
    LOG_E("flt", "kept-e");
    log_shutdown();
    file_read(path, buf, sizeof buf);
    CHECK_EQ_INT(count_char(buf, '\n'), 2);
    CHECK(strstr(buf, "dropped") == NULL);
    CHECK(strstr(buf, " WARN flt: kept-w\n") != NULL);
    CHECK(strstr(buf, " ERROR flt: kept-e\n") != NULL);
    fixture_drop(dir, path);
}

static void test_set_level_changes_filter_at_runtime(void)
{
    char dir[FIX_CAP], path[FIX_CAP], buf[BUF_CAP];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(log_init(LOG_ERROR, path), 0);
    LOG_W("rt", "dropped-w");
    log_set_level(LOG_DEBUG);
    LOG_D("rt", "kept-d");
    log_set_level(LOG_ERROR);
    LOG_W("rt", "dropped-w2");
    LOG_E("rt", "kept-e");
    log_shutdown();
    file_read(path, buf, sizeof buf);
    CHECK_EQ_INT(count_char(buf, '\n'), 2);
    CHECK(strstr(buf, "dropped") == NULL);
    CHECK(strstr(buf, " DEBUG rt: kept-d\n") != NULL);
    CHECK(strstr(buf, " ERROR rt: kept-e\n") != NULL);
    fixture_drop(dir, path);
}

static long long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L;
}

static long long parse_ts_ms(const char *line)
{
    struct tm tm;
    int ms = 0;

    memset(&tm, 0, sizeof tm);
    if (sscanf(line, "%d-%d-%dT%d:%d:%d.%dZ", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) != 7) {
        return -1;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return (long long)timegm(&tm) * 1000LL + ms;
}

static void test_timestamp_is_utc_ms_within_call_window(void)
{
    char dir[FIX_CAP], path[FIX_CAP], buf[BUF_CAP];
    long long before, after;

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(log_init(LOG_DEBUG, path), 0);
    before = now_ms();
    LOG_I("time", "tick");
    after = now_ms();
    log_shutdown();
    file_read(path, buf, sizeof buf);
    CHECK(parse_ts_ms(buf) >= before);
    CHECK(parse_ts_ms(buf) <= after);
    fixture_drop(dir, path);
}

static void test_path_writes_to_stderr_and_file(void)
{
    char dir[FIX_CAP], path[FIX_CAP], err[FIX_CAP], file[BUF_CAP], cap[BUF_CAP];
    int saved, fd;

    FIXTURE_OR_RETURN(dir, path);
    memcpy(err, path, strlen(path) + 1);
    strcat(err, ".err");
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    saved = dup(STDERR_FILENO);
    fd = open(err, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    dup2(fd, STDERR_FILENO);
    close(fd);
    LOG_I("both", "line %d", 7);
    dup2(saved, STDERR_FILENO);
    close(saved);
    log_shutdown();
    file_read(path, file, sizeof file);
    file_read(err, cap, sizeof cap);
    CHECK(rx_matches(LINE_RE "both: line 7\n$", file));
    CHECK(rx_matches(LINE_RE "both: line 7\n$", cap));
    unlink(err);
    fixture_drop(dir, path);
}

int main(void)
{
    RUN(test_pre_init_writes_info_to_stderr);
    RUN(test_line_format_matches_regex);
    RUN(test_all_levels_emitted_at_debug_min);
    RUN(test_min_level_filters_lower_levels);
    RUN(test_set_level_changes_filter_at_runtime);
    RUN(test_timestamp_is_utc_ms_within_call_window);
    RUN(test_path_writes_to_stderr_and_file);
    return test_summary();
}
