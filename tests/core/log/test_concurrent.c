#define _GNU_SOURCE
#include "core/log/log_test_util.h"

#include <pthread.h>

#define THREADS 8
#define PER_THREAD 1000
#define PAYLOAD_LEN 200
#define OUT_CAP (4 * 1024 * 1024)
#define COPY_CAP 1100

static char g_out[OUT_CAP];
static unsigned char g_seen[THREADS][PER_THREAD];

static void *writer(void *arg)
{
    const int *id = arg;
    char payload[PAYLOAD_LEN + 1];

    memset(payload, 'x', PAYLOAD_LEN);
    payload[PAYLOAD_LEN] = '\0';
    for (int i = 0; i < PER_THREAD; ++i) {
        LOG_I("conc", "thread=%d seq=%04d %s end", *id, i, payload);
    }
    return NULL;
}

static int check_line(const char *line, size_t len)
{
    char copy[COPY_CAP];
    int t = -1;
    int s = -1;
    const char *tag;

    if (len >= sizeof copy) {
        return 0;
    }
    memcpy(copy, line, len);
    copy[len] = '\0';
    if (!rx_matches(LINE_RE "conc: thread=[0-7] seq=[0-9]{4} x{200} end$", copy)) {
        return 0;
    }
    tag = strstr(copy, "thread=");
    if (tag == NULL || sscanf(tag, "thread=%d seq=%d", &t, &s) != 2) {
        return 0;
    }
    if (g_seen[t][s] != 0) {
        return 0;
    }
    g_seen[t][s] = 1;
    return 1;
}

static int check_file_lines(const char *buf)
{
    const char *p = buf;
    const char *nl;
    int ok = 0;

    while (*p != '\0') {
        nl = strchr(p, '\n');
        ok += check_line(p, nl != NULL ? (size_t)(nl - p) : strlen(p));
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return ok;
}

static void start_writers(pthread_t *th, int *ids)
{
    for (int t = 0; t < THREADS; ++t) {
        ids[t] = t;
        CHECK_EQ_INT(pthread_create(&th[t], NULL, writer, &ids[t]), 0);
    }
}

static void join_writers(pthread_t *th)
{
    for (int t = 0; t < THREADS; ++t) {
        CHECK_EQ_INT(pthread_join(th[t], NULL), 0);
    }
}

static void test_concurrent_writes_stay_whole_lines(void)
{
    char dir[FIX_CAP], path[FIX_CAP];
    pthread_t th[THREADS];
    int ids[THREADS];

    FIXTURE_OR_RETURN(dir, path);
    CHECK_EQ_INT(log_init(LOG_INFO, path), 0);
    start_writers(th, ids);
    join_writers(th);
    log_shutdown();
    file_read(path, g_out, sizeof g_out);
    CHECK_EQ_INT(count_char(g_out, '\n'), THREADS * PER_THREAD);
    CHECK_EQ_INT(check_file_lines(g_out), THREADS * PER_THREAD);
    fixture_drop(dir, path);
}

int main(void)
{
    RUN(test_concurrent_writes_stay_whole_lines);
    return test_summary();
}
