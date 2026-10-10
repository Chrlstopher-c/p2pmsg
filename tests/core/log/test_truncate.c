#define _GNU_SOURCE
#include "core/log/log_test_util.h"

#define BUF_CAP 4096
#define MSG_CAP 8192
#define PREFIX_LEN 33 /* "TS(24) INFO u: " = 24 + 1 + 4 + 1 + 1 + 2 */
#define EURO "\xE2\x82\xAC"

static void log_msg(const char *path, const char *msg)
{
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    LOG_I("u", "%s", msg);
    log_shutdown();
}

static void build_ascii_then_euro(char *msg, size_t ascii_count)
{
    memset(msg, 'a', ascii_count);
    memcpy(msg + ascii_count, EURO, 3);
    msg[ascii_count + 3] = '\0';
}

static void test_long_line_truncated_to_1024_bytes(void)
{
    char dir[FIX_CAP], path[FIX_CAP], buf[BUF_CAP], msg[MSG_CAP];

    FIXTURE_OR_RETURN(dir, path);
    memset(msg, 'x', 5000);
    msg[5000] = '\0';
    log_msg(path, msg);
    CHECK_EQ_INT(file_size(path), 1024);
    file_read(path, buf, sizeof buf);
    CHECK(buf[1023] == '\n');
    CHECK(buf[1022] == 'x');
    CHECK_EQ_INT(count_char(buf, '\n'), 1);
    CHECK(rx_matches(LINE_RE "u: x+\n$", buf));
    fixture_drop(dir, path);
}

static void test_exact_fit_keeps_full_message(void)
{
    char dir[FIX_CAP], path[FIX_CAP], buf[BUF_CAP], msg[MSG_CAP];

    FIXTURE_OR_RETURN(dir, path);
    memset(msg, 'a', 1023 - PREFIX_LEN);
    msg[1023 - PREFIX_LEN] = '\0';
    log_msg(path, msg);
    CHECK_EQ_INT(file_size(path), 1024);
    file_read(path, buf, sizeof buf);
    CHECK(buf[1022] == 'a');
    CHECK(buf[1023] == '\n');
    fixture_drop(dir, path);
}

static void test_cut_never_splits_utf8_sequence_mid_char(void)
{
    char dir[FIX_CAP], path[FIX_CAP], buf[BUF_CAP], msg[MSG_CAP];

    FIXTURE_OR_RETURN(dir, path);
    build_ascii_then_euro(msg, 1022 - PREFIX_LEN);
    log_msg(path, msg);
    CHECK_EQ_INT(file_size(path), 1023);
    file_read(path, buf, sizeof buf);
    CHECK(buf[1021] == 'a');
    CHECK(buf[1022] == '\n');
    fixture_drop(dir, path);
}

static void test_cut_drops_whole_char_when_it_straddles_limit(void)
{
    char dir[FIX_CAP], path[FIX_CAP], buf[BUF_CAP], msg[MSG_CAP];

    FIXTURE_OR_RETURN(dir, path);
    build_ascii_then_euro(msg, 1021 - PREFIX_LEN);
    log_msg(path, msg);
    CHECK_EQ_INT(file_size(path), 1022);
    file_read(path, buf, sizeof buf);
    CHECK(buf[1020] == 'a');
    CHECK(buf[1021] == '\n');
    fixture_drop(dir, path);
}

int main(void)
{
    RUN(test_long_line_truncated_to_1024_bytes);
    RUN(test_exact_fit_keeps_full_message);
    RUN(test_cut_never_splits_utf8_sequence_mid_char);
    RUN(test_cut_drops_whole_char_when_it_straddles_limit);
    return test_summary();
}
