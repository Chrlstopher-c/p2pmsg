/* sec/session — couche interne Noise (privée au module) : wrapper autour de NoiseHandshakeState / NoiseCipherState.
 * Gère le handshake XX, le split, le chiffrement de transport avec compteur par sens et le rekey à
 * SESS_REKEY_EVERY messages. N'est pas exposée hors de sec/session. */
#ifndef P2P_SEC_SESSION_SESS_NOISE_H
#define P2P_SEC_SESSION_SESS_NOISE_H
#include <stddef.h>
#include <stdint.h>
#include "sec/session/session.h"

#define SN_MAC_LEN 16u

typedef struct sn sn_t;

typedef struct {
    const uint8_t *static_priv; /* 32 octets, obligatoire */
    const uint8_t *eph_priv;    /* 32 octets fixes (vecteurs de test) ; NULL = éphémère généré */
    const void    *prologue;
    size_t         prologue_len;
} sn_opts;

sn_t *sn_new(sess_role role, const sn_opts *o);       /* NULL si échec */
void  sn_free(sn_t *n);                               /* efface les clés locales ; accepte NULL */
/* Messages de handshake. out >= payload + 96 octets. 0 ou SESS_EPROTO / SESS_EDECRYPT. */
int   sn_write_hs(sn_t *n, const void *payload, size_t plen, uint8_t *out, size_t cap, size_t *out_len);
int   sn_read_hs(sn_t *n, const uint8_t *in, size_t in_len, uint8_t *payload, size_t cap, size_t *plen);
/* Clé statique du pair, connue après lecture du message qui la porte. 0 ou -1. */
int   sn_remote_static(const sn_t *n, uint8_t out[SESS_KEY_LEN]);
int   sn_hash(const sn_t *n, uint8_t *out, size_t len); /* hash de handshake, 0 ou -1 */
int   sn_split(sn_t *n);                                /* 0 ou SESS_EPROTO (handshake non terminé) */
/* Transport. ct = len + SN_MAC_LEN octets. 0 ou SESS_EPROTO. */
int   sn_encrypt(sn_t *n, const uint8_t *pt, size_t len, uint8_t *ct);
/* pt = len - SN_MAC_LEN octets. 0, SESS_EDECRYPT (MAC invalide) ou SESS_EPROTO (trop court). */
int   sn_decrypt(sn_t *n, const uint8_t *ct, size_t len, uint8_t *pt);

#endif
