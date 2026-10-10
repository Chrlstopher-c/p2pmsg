/* core/log : niveaux, sortie stderr ou fichier 0600. Une ligne = un write() sous mutex. */
#define _GNU_SOURCE
#include "core/log/log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG_LINE_MAX 1024
#define LOG_SCRATCH 2048
#define LOG_MODULE_MAX 64

/* Module NULL journalisé comme "?". Ligne = contenu + "\n" = 1024 octets max (le \n compte). */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static log_level g_min = LOG_INFO;
static int g_fd = -1; /* -1 = pas de fichier (stderr seul) */

static const char *const LEVEL_NAMES[] = { "DEBUG", "INFO", "WARN", "ERROR" };

static void close_file_locked(void)
{
    if (g_fd >= 0) {
        close(g_fd);
    }
    g_fd = -1;
}

static int open_log_file(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    int err;

    if (fd < 0) {
        return -errno;
    }
    if (fchmod(fd, 0600) != 0) {
        err = errno;
        close(fd);
        return -err;
    }
    return fd;
}

static size_t format_prefix(char *buf, log_level lvl, const char *module)
{
    struct timespec ts;
    struct tm utc;
    size_t n;
    int m;

    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &utc);
    n = strftime(buf, LOG_SCRATCH, "%Y-%m-%dT%H:%M:%S", &utc);
    m = snprintf(buf + n, LOG_SCRATCH - n, ".%03ldZ %s %.*s: ", ts.tv_nsec / 1000000L,
                 LEVEL_NAMES[lvl], LOG_MODULE_MAX, module);
    return n + (size_t)m;
}

/* Recule jusqu'au début du caractère UTF-8 contenant limit, pour ne jamais couper une séquence. */
static size_t utf8_cut(const char *buf, size_t limit)
{
    while (limit > 0 && ((unsigned char)buf[limit] & 0xC0) == 0x80) {
        limit--;
    }
    return limit;
}

static size_t format_line(char *buf, log_level lvl, const char *module, const char *fmt, va_list ap)
{
    size_t len = format_prefix(buf, lvl, module);
    int need = vsnprintf(buf + len, LOG_SCRATCH - len, fmt, ap);

    if (need > 0) {
        size_t room = LOG_SCRATCH - len - 1;
        len += (size_t)need < room ? (size_t)need : room;
    }
    if (len > LOG_LINE_MAX - 2) {
        len = utf8_cut(buf, LOG_LINE_MAX - 1);
    }
    buf[len] = '\n';
    return len + 1;
}

/* Échec d'écriture du log : rien où journaliser, la ligne est abandonnée sans bloquer l'appelant. */
static void write_all(int fd, const char *buf, size_t len)
{
    ssize_t w;

    while (len > 0) {
        w = write(fd, buf, len);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        buf += w;
        len -= (size_t)w;
    }
}

int log_init(log_level min, const char *path)
{
    int rc = 0;

    pthread_mutex_lock(&g_mu);
    close_file_locked();
    g_min = min;
    if (path != NULL) {
        rc = open_log_file(path);
        if (rc >= 0) {
            g_fd = rc;
            rc = 0;
        }
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

void log_shutdown(void)
{
    pthread_mutex_lock(&g_mu);
    close_file_locked();
    pthread_mutex_unlock(&g_mu);
}

void log_set_level(log_level min)
{
    pthread_mutex_lock(&g_mu);
    g_min = min;
    pthread_mutex_unlock(&g_mu);
}

void log_write(log_level lvl, const char *module, const char *fmt, ...)
{
    char buf[LOG_SCRATCH];
    size_t len = 0;
    va_list ap;

    pthread_mutex_lock(&g_mu);
    if (lvl >= g_min) {
        va_start(ap, fmt);
        len = format_line(buf, lvl, module != NULL ? module : "?", fmt, ap);
        va_end(ap);
        write_all(STDERR_FILENO, buf, len);
        if (g_fd >= 0) {
            write_all(g_fd, buf, len);
        }
    }
    pthread_mutex_unlock(&g_mu);
}
