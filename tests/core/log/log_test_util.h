/* Helpers partagés des tests core/log : fixtures fichiers temporaires, lecture, regex POSIX. */
#ifndef P2P_TESTS_CORE_LOG_UTIL_H
#define P2P_TESTS_CORE_LOG_UTIL_H

#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/log/log.h"
#include "test.h"

#define FIX_CAP 256
#define TS_RE "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3}Z"
#define LINE_RE "^" TS_RE " (DEBUG|INFO|WARN|ERROR) "

static inline int fixture_make(char *dir, char *path, size_t cap)
{
    size_t dlen;

    snprintf(dir, cap, "/tmp/p2p-log-test-XXXXXX");
    if (mkdtemp(dir) == NULL) {
        return -1;
    }
    dlen = strlen(dir);
    if (dlen + sizeof("/log.txt") > cap) {
        return -1;
    }
    memcpy(path, dir, dlen);
    memcpy(path + dlen, "/log.txt", sizeof("/log.txt"));
    return 0;
}

#define FIXTURE_OR_RETURN(dir, path) do { \
    if (fixture_make((dir), (path), sizeof(dir)) != 0) { \
        CHECK(0); \
        return; \
    } \
} while (0)

static inline void fixture_drop(const char *dir, const char *path)
{
    unlink(path);
    rmdir(dir);
}

static inline int file_write(const char *path, const char *data)
{
    FILE *f = fopen(path, "wb");
    size_t len = strlen(data);
    size_t n;

    if (f == NULL) {
        return -1;
    }
    n = fwrite(data, 1, len, f);
    fclose(f);
    return n == len ? 0 : -1;
}

static inline long file_read(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        buf[0] = '\0';
        return -1;
    }
    n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
    return (long)n;
}

static inline long file_size(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 ? (long)st.st_size : -1L;
}

static inline int file_mode(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 ? (int)(st.st_mode & 0777) : -1;
}

static inline int rx_matches(const char *pattern, const char *text)
{
    regex_t re;
    int rc;

    if (regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
        fprintf(stderr, "regcomp échoué : %s\n", pattern);
        return 0;
    }
    rc = regexec(&re, text, 0, NULL, 0);
    regfree(&re);
    return rc == 0;
}

static inline int count_char(const char *s, char c)
{
    int n = 0;

    for (; *s != '\0'; ++s) {
        n += (*s == c);
    }
    return n;
}

#endif
