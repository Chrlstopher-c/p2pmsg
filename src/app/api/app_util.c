/* app/api : utilitaires (chemins, dossier 0700, identifiants aléatoires, horloge, nom d'utilisateur). */
#define _GNU_SOURCE
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app/api/app_internal.h"
#include "core/log/log.h"

#define APP_DIR_MODE 0700
#define APP_USER_FALLBACK "anonyme"

int app_resolve_dir(const char *dir, char *out, size_t cap)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    int n;

    if (dir != NULL) {
        n = snprintf(out, cap, "%s", dir);
    } else if (xdg != NULL && xdg[0] != '\0') {
        n = snprintf(out, cap, "%s/p2pmsg", xdg);
    } else if (home != NULL) {
        n = snprintf(out, cap, "%s/.local/share/p2pmsg", home);
    } else {
        LOG_E(APP_MOD, "dossier de données indéterminé (ni dir, ni HOME)");
        return -ENOENT;
    }
    return (n > 0 && (size_t)n < cap) ? 0 : -ENAMETOOLONG;
}

int app_make_dir(const char *path)
{
    if (mkdir(path, APP_DIR_MODE) == 0 || errno == EEXIST) {
        return 0;
    }
    LOG_E(APP_MOD, "création du dossier impossible path=%s err=%d", path, errno);
    return -errno;
}

uint64_t app_random_id(void)
{
    uint64_t v = 0;

    do {
        if (getrandom(&v, sizeof v, 0) != (ssize_t)sizeof v) {
            LOG_E(APP_MOD, "getrandom échoué err=%d", errno);
            return 0;
        }
    } while (v == 0);
    return v;
}

uint64_t app_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

void app_user_name(char *out, size_t cap)
{
    struct passwd *pw = getpwuid(getuid());
    const char *name = (pw != NULL && pw->pw_name != NULL) ? pw->pw_name : APP_USER_FALLBACK;

    snprintf(out, cap, "%s", name);
}
