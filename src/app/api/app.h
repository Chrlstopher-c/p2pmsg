/* app/api — API publique de l'application, seule interface de ui/gtk et app/cli.
 * Modèle de threads : app_start lance la boucle réseau dans un thread dédié. Les évènements sont appelés DANS ce
 * thread (l'UI doit les rapatrier, ex. g_idle_add). Toutes les fonctions app_* sont thread-safe.
 * Données : <dir>/identity.key (0600), <dir>/history.db, <dir>/peers (liste statique optionnelle), <dir>/p2pmsg.log.
 * Au démarrage : pairs persistés → pm_add_known, messages PENDING → réinjectés (même id) dans le manager. */
#ifndef P2P_APP_API_H
#define P2P_APP_API_H
#include <stddef.h>
#include <stdint.h>

#define APP_KEY_HEX 65u

typedef enum { APP_OFFLINE, APP_CONNECTING, APP_CONNECTED } app_peer_state;
typedef enum { APP_MSG_PENDING, APP_MSG_DELIVERED, APP_MSG_FAILED, APP_MSG_RECEIVED } app_msg_status;

typedef struct {
    char           key[APP_KEY_HEX];
    char           name[65];
    app_peer_state state;
} app_peer;

typedef struct {
    char           peer[APP_KEY_HEX];
    uint64_t       id;
    int            outgoing;
    uint64_t       ts_ms;
    const char    *text;   /* valide pendant le callback seulement */
    app_msg_status status;
} app_message;

typedef struct {
    void (*on_peer)(const app_peer *p, void *ud);
    void (*on_message)(const app_message *m, void *ud);              /* nouveau message reçu */
    void (*on_status)(const char *peer, uint64_t id, app_msg_status st, void *ud);
} app_events;

typedef struct {
    const char *dir;          /* NULL = $XDG_DATA_HOME/p2pmsg ou ~/.local/share/p2pmsg */
    const char *name;         /* NULL = nom d'utilisateur */
    const char *listen_host;  /* NULL = "0.0.0.0" */
    uint16_t    listen_port;  /* 0 = 47800 ; si occupé → erreur */
    int         enable_mdns;
    int         log_debug;
} app_config;

typedef struct app app_t;

app_t *app_new(const app_config *cfg, const app_events *ev, void *ud); /* NULL si échec (raison loggée) */
int    app_start(app_t *a);
void   app_stop(app_t *a);   /* BYE, arrêt du thread, bloquant */
void   app_free(app_t *a);

const char *app_self_key(const app_t *a);   /* hex, valide pendant la vie de a */
uint16_t    app_listen_port(const app_t *a);
/* Persiste en PENDING puis envoie. *out_id = id aléatoire non nul. 0 ou <0. */
int  app_send(app_t *a, const char *peer_hex, const char *text, uint64_t *out_id);
int  app_peers(app_t *a, app_peer *out, int max);  /* instantané ; nombre écrit */
/* Historique (ts décroissant), cb appelé dans le thread appelant. */
int  app_history(app_t *a, const char *peer_hex, uint64_t before_ms, int limit,
                 void (*cb)(const app_message *m, void *ud), void *ud);

#endif
