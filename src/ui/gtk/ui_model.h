/* ui/gtk — modèle pur (sans GTK) : liste des pairs, conversation courante, formatage, décisions de saisie. */
#ifndef P2P_UI_GTK_MODEL_H
#define P2P_UI_GTK_MODEL_H
#include <stddef.h>
#include <stdint.h>

#include "app/api/app.h"

#define UI_PEERS_MAX     256u
#define UI_CONV_MAX      500u
#define UI_HISTORY_PAGE  50
#define UI_TEXT_BYTES    4096u
#define UI_TIME_LEN      6u

typedef enum { UI_KEY_IGNORE, UI_KEY_SEND, UI_KEY_NEWLINE } ui_key_action;

typedef struct {
    uint64_t       id;
    int            outgoing;
    uint64_t       ts_ms;
    app_msg_status status;
    char          *text;   /* possédé par le modèle */
} ui_bubble;

typedef struct {
    app_peer  peers[UI_PEERS_MAX];
    unsigned  unread[UI_PEERS_MAX];
    size_t    n_peers;
    char      current[APP_KEY_HEX];
    ui_bubble bubbles[UI_CONV_MAX];
    size_t    n_bubbles;
} ui_model;

void        ui_model_init(ui_model *m);
void        ui_model_clear_conv(ui_model *m);
/* Insère ou met à jour un pair. Retourne son index, -1 si liste pleine ; *is_new = 1 si ajouté. */
int         ui_model_upsert_peer(ui_model *m, const app_peer *p, int *is_new);
int         ui_model_find_peer(const ui_model *m, const char *key);
const char *ui_model_current(const ui_model *m);
void        ui_model_set_current(ui_model *m, const char *key);
/* Ajoute à la fin (ordre chronologique). Copie le texte. Retourne l'index, -1 si échec ;
 * *evicted = 1 si le plus ancien a été retiré pour faire place (les index décalent de 1). */
int         ui_model_append(ui_model *m, const app_message *msg, int *evicted);
/* Insère en tête (historique reçu du plus récent au plus ancien). -1 si plein ou échec. */
int         ui_model_prepend(ui_model *m, const app_message *msg);
int         ui_model_find_id(const ui_model *m, uint64_t id);
/* Met à jour le statut d'un message de la conversation courante. Index ou -1. */
int         ui_model_set_status(ui_model *m, const char *peer, uint64_t id, app_msg_status st);

/* "HH:MM" en heure locale. 0 ou -1. */
int         ui_format_time(uint64_t ts_ms, char *out, size_t cap);
const char *ui_status_glyph(app_msg_status st);
const char *ui_peer_css_class(app_peer_state st);
const char *ui_peer_status_text(app_peer_state st);
void        ui_model_add_unread(ui_model *m, int idx);
void        ui_model_clear_unread(ui_model *m, int idx);
/* is_enter : Entrée (principale ou pavé). shift : Maj enfoncée. */
ui_key_action ui_key_decide(int is_enter, int shift);
/* Non vide après retrait des blancs, et <= UI_TEXT_BYTES. */
int         ui_text_sendable(const char *text);

#endif
