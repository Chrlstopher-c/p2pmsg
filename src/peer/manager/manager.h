/* peer/manager — état des pairs, connexions (sortantes + entrantes), reconnexion, file d'envoi, ACK, keepalive.
 * Ne touche pas au stockage : l'appelant (app) persiste et réinjecte les pending via pm_send.
 * Règles :
 * - Pair connu = vu par discovery. Entrant d'une clé inconnue, ou clé ≠ celle attendue pour l'adresse composée
 *   (sortant) → rejet (SESS_EREJECTED) + LOG_W (empreinte 8 hex seulement).
 * - Une seule session par pair : en cas de double connexion, on garde celle initiée par la clé la plus petite (memcmp).
 * - Après établissement : chacun envoie HELLO ; pair → PM_CONNECTED à la réception du HELLO.
 * - Reconnexion sortante : backoff 1 s, ×2, max 30 s, remis à 1 s après succès.
 * - PING toutes les 15 s sans trafic sortant ; aucune trame reçue depuis 45 s → fermeture puis reconnexion.
 * - MSG : retransmis si pas d'ACK après 10 s, 5 envois max au total → on_failed. File conservée hors connexion
 *   (le délai de 10 s ne court que connecté). Côté réception : ACK toujours renvoyé, doublons (1024 derniers ids
 *   par pair) non remontés.
 * - Logs : jamais de texte de message. */
#ifndef P2P_PEER_MANAGER_H
#define P2P_PEER_MANAGER_H
#include <stdint.h>
#include "core/loop/loop.h"
#include "sec/session/session.h"
#include "net/discovery/discovery.h"

#define PM_RETRY_MS   10000u
#define PM_MAX_SENDS  5u
#define PM_PING_MS    15000u
#define PM_DEAD_MS    45000u
#define PM_QUEUE_MAX  4096u /* messages en attente, tous pairs confondus */

typedef enum { PM_OFFLINE, PM_CONNECTING, PM_CONNECTED } pm_state;

typedef struct {
    void (*on_peer)(const uint8_t pub[32], const char *name, pm_state st, void *ud);
    void (*on_message)(const uint8_t pub[32], uint64_t id, uint64_t ts_ms, const char *text, void *ud);
    void (*on_delivered)(const uint8_t pub[32], uint64_t id, void *ud);
    void (*on_failed)(const uint8_t pub[32], uint64_t id, void *ud);
} pm_events;

typedef struct {
    const sess_identity *identity;
    const char *name;
    const char *listen_host;  /* "0.0.0.0" par défaut si NULL */
    uint16_t    listen_port;  /* 0 = éphémère */
    disc_config disc;         /* name/pubkey/port remplis par pm_new depuis les champs ci-dessus */
    /* Tests seulement : facteur de temps (1 = réel). Divise tous les délais ci-dessus. */
    uint32_t    time_div;
} pm_config;

typedef struct pm pm_t;

pm_t    *pm_new(loop_t *l, const pm_config *cfg, const pm_events *ev, void *ud);
void     pm_free(pm_t *pm);                  /* BYE aux pairs connectés, fermeture */
uint16_t pm_listen_port(const pm_t *pm);
/* Met en file. id choisi par l'appelant (aléatoire non nul). text UTF-8 ≤ MSG_TEXT_MAX.
 * 0, -EINVAL, -ENOSPC (file pleine), -ENOENT (pair jamais vu). */
int      pm_send(pm_t *pm, const uint8_t pub[32], uint64_t id, uint64_t ts_ms, const char *text);
pm_state pm_peer_state(const pm_t *pm, const uint8_t pub[32]);
/* Ajoute un pair connu hors discovery (ex. pairs persistés). Sans adresse : seulement accepté en entrant. */
int      pm_add_known(pm_t *pm, const uint8_t pub[32], const char *name, const char *host, uint16_t port);

#endif
