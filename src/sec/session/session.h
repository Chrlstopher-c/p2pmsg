/* sec/session — Noise_XX_25519_ChaChaPoly_BLAKE2b (noise-c) au-dessus d'une tcp_conn. Seul module à toucher noise-c.
 * Fil : trames proto/frame. Type 0x01 = message de handshake (payload brut Noise). Type 0x02 = transport :
 * payload = chiffré( u8 inner_type | inner_payload ). Prologue "p2pmsg/1". Trame 0x02 avant établissement,
 * trame 0x01 après, type inconnu, échec de déchiffrement → fermeture immédiate (on_close err négatif).
 * Rekey : noise_cipherstate_rekey (sans signalisation) après SESS_REKEY_EVERY messages dans chaque sens. */
#ifndef P2P_SEC_SESSION_H
#define P2P_SEC_SESSION_H
#include <stddef.h>
#include <stdint.h>
#include "net/tcp/tcp.h"
#include "proto/frame/frame.h"

#define SESS_KEY_LEN       32u
#define SESS_FRAME_HS      0x01
#define SESS_FRAME_DATA    0x02
#define SESS_REKEY_EVERY   10000u
#define SESS_PLAINTEXT_MAX (FRAME_PAYLOAD_MAX - 16u - 1u) /* inner payload max */
#define SESS_HS_TIMEOUT_MS 10000u

#define SESS_EREJECTED (-1401) /* clé du pair refusée par on_verify */
#define SESS_EPROTO    (-1402) /* trame inattendue / handshake invalide */
#define SESS_EDECRYPT  (-1403)
#define SESS_ETIMEOUT  (-1404)

typedef struct { uint8_t pub[SESS_KEY_LEN]; uint8_t priv[SESS_KEY_LEN]; } sess_identity;
typedef struct sess sess_t;
typedef enum { SESS_INITIATOR, SESS_RESPONDER } sess_role;

typedef struct {
    /* Après réception de la clé statique du pair. Retour 0 = accepter, sinon rejet (SESS_EREJECTED). */
    int  (*on_verify)(sess_t *s, const uint8_t peer_pub[SESS_KEY_LEN], void *ud);
    void (*on_established)(sess_t *s, void *ud);
    void (*on_message)(sess_t *s, uint8_t type, const uint8_t *payload, size_t len, void *ud);
    /* Toujours appelé exactement une fois ; s libéré au retour (la tcp_conn aussi). */
    void (*on_close)(sess_t *s, int err, void *ud);
} sess_handlers;

/* Charge la clé (32 octets priv bruts) ou la crée (fichier 0600, dossier créé si absent). 0 ou -errno/-EINVAL. */
int sess_identity_load_or_create(const char *path, sess_identity *out);
/* Hex minuscule 64 car + '\0'. */
void sess_key_to_hex(const uint8_t key[SESS_KEY_LEN], char out[2 * SESS_KEY_LEN + 1]);
int  sess_key_from_hex(const char *hex, uint8_t key[SESS_KEY_LEN]); /* 0 ou -EINVAL */

/* Prend possession de c (remplace ses handlers). L'initiateur envoie le 1er message tout de suite
 * (ou dès on_connect si c n'est pas encore connectée : passer connected=0). NULL si échec (c fermée). */
sess_t *sess_new(loop_t *l, tcp_conn_t *c, int connected, sess_role role, const sess_identity *id,
                 const sess_handlers *h, void *ud);
int  sess_send(sess_t *s, uint8_t type, const void *payload, size_t len); /* 0, -ENOTCONN, TCP_ETXFULL, -EMSGSIZE */
void sess_close(sess_t *s, int graceful); /* on_close(0) asynchrone */
int  sess_is_established(const sess_t *s);
const uint8_t *sess_peer_key(const sess_t *s); /* NULL avant on_verify */
tcp_conn_t *sess_conn(sess_t *s);

#endif
