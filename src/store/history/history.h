/* store/history — SQLite3 (WAL, synchronous=NORMAL, busy_timeout 2000). Seul module à toucher sqlite3.
 * Schéma : peers(id INTEGER PK, name TEXT, pubkey BLOB UNIQUE NOT NULL, last_seen INTEGER)
 *          messages(id INTEGER NOT NULL, peer_id INTEGER NOT NULL REFERENCES peers, direction INTEGER, ts INTEGER,
 *                   body TEXT, status INTEGER, PRIMARY KEY(peer_id, id, direction))
 * id message = u64 du protocole stocké en INTEGER (cast bit à bit int64). Une connexion par hist_t ;
 * un hist_t n'est pas partagé entre threads (ouvrir un hist_t par thread sur le même fichier). */
#ifndef P2P_STORE_HISTORY_H
#define P2P_STORE_HISTORY_H
#include <stdint.h>

typedef enum { HIST_OUT = 0, HIST_IN = 1 } hist_dir;
typedef enum { HIST_PENDING = 0, HIST_DELIVERED = 1, HIST_FAILED = 2, HIST_RECEIVED = 3 } hist_status;

typedef struct {
    uint8_t     peer[32];
    uint64_t    id;
    hist_dir    dir;
    uint64_t    ts_ms;
    const char *body;     /* valide pendant le callback seulement */
    hist_status status;
} hist_msg;

typedef struct {
    uint8_t     pubkey[32];
    const char *name;     /* valide pendant le callback seulement */
    uint64_t    last_seen_ms;
} hist_peer;

typedef struct hist hist_t;
typedef int (*hist_msg_cb)(const hist_msg *m, void *ud);   /* retour ≠ 0 = arrêt de l'itération */
typedef int (*hist_peer_cb)(const hist_peer *p, void *ud);

hist_t *hist_open(const char *path);  /* crée schéma si absent ; fichier 0600 ; NULL si échec (LOG_E) */
void    hist_close(hist_t *h);

int hist_upsert_peer(hist_t *h, const uint8_t pub[32], const char *name, uint64_t last_seen_ms);
/* Insertion idempotente : doublon (peer,id,dir) → 1 sans modification ; 0 si inséré ; <0 erreur. */
int hist_add(hist_t *h, const hist_msg *m);
int hist_set_status(hist_t *h, const uint8_t peer[32], uint64_t id, hist_dir dir, hist_status st);
/* Messages d'un pair, ts décroissant, ts < before_ms (UINT64_MAX = depuis la fin), au plus limit. */
int hist_list(hist_t *h, const uint8_t peer[32], uint64_t before_ms, int limit, hist_msg_cb cb, void *ud);
int hist_pending(hist_t *h, hist_msg_cb cb, void *ud);   /* OUT + PENDING, ts croissant */
int hist_peers(hist_t *h, hist_peer_cb cb, void *ud);

#endif
