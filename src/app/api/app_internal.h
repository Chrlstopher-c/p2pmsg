/* app/api (interne) — état partagé entre app.c (API appelante) et app_net.c (thread réseau). */
#ifndef P2P_APP_API_INTERNAL_H
#define P2P_APP_API_INTERNAL_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "app/api/app.h"
#include "app/api/app_queue.h"
#include "core/loop/loop.h"
#include "peer/manager/manager.h"
#include "sec/session/session.h"
#include "store/history/history.h"

#define APP_MOD        "app/api"
#define APP_PEERS_MAX  256u
#define APP_PATH_MAX  256u
#define APP_FILE_PATH_MAX 512u
#define APP_NAME_MAX   64u
#define APP_DEFAULT_PORT 47800u

struct app {
    app_events       ev;
    void            *ud;
    char             dir[APP_PATH_MAX];
    char             name[APP_NAME_MAX + 1];
    int              enable_mdns;
    char             peers_path[APP_FILE_PATH_MAX]; /* "" = pas de liste statique */
    sess_identity    id;
    char             self_hex[APP_KEY_HEX];
    uint16_t         port;
    loop_t          *loop;            /* possédé par le thread réseau une fois démarré */
    pm_t            *pm;              /* idem */
    hist_t          *net_hist;        /* idem : jamais partagé avec un autre thread */
    hist_t          *ui_hist;         /* app_history, protégé par hist_mu */
    pthread_mutex_t  hist_mu;
    pthread_mutex_t  peers_mu;
    int              sync_ready;      /* 1 = mutex et file initialisés (libérés par app_free) */
    app_queue        queue;
    app_peer         peers[APP_PEERS_MAX]; /* snapshot, protégé par peers_mu */
    size_t           npeers;
    atomic_int       stop_req;
    pthread_t        thread;
    int              running;         /* appelant seulement */
};

/* app_util.c */
int      app_resolve_dir(const char *dir, char *out, size_t cap);
int      app_make_dir(const char *path);
uint64_t app_random_id(void);       /* aléatoire non nul ; 0 si getrandom échoue (loggé) */
uint64_t app_now_ms(void);          /* horloge murale, ms */
void     app_user_name(char *out, size_t cap);

/* app_net.c */
int  net_init(app_t *a, const app_config *cfg);   /* loop, pm, hist réseau ; appelant = thread de création */
int  net_start(app_t *a);                          /* lance le thread réseau */
void net_stop(app_t *a);                           /* demande l'arrêt et joint le thread */
void net_release(app_t *a);                        /* libère pm/loop/hist réseau (après arrêt du thread) */

#endif
