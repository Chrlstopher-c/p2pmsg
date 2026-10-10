/* sec/session — wrapper Noise (privé). Seul fichier qui inclut les en-têtes noise-c du module. */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <noise/protocol/buffer.h>
#include <noise/protocol/cipherstate.h>
#include <noise/protocol/constants.h>
#include <noise/protocol/dhstate.h>
#include <noise/protocol/errors.h>
#include <noise/protocol/handshakestate.h>
#include "sec/session/sess_noise.h"

#define SN_PROTO_NAME   "Noise_XX_25519_ChaChaPoly_BLAKE2b"
#define SN_HS_MAX       1024u
static const uint8_t NO_PL[1] = {0};
#define SN_REKEY_NONCE  (0xFFFFFFFFFFFFFFFEULL) /* 2^64 - 2 : jamais utilisé par le transport */

struct sn {
    NoiseHandshakeState *hs;
    NoiseCipherState    *tx;
    NoiseCipherState    *rx;
    uint64_t             tx_count;
    uint64_t             rx_count;
};

static void sn_drop_hs(sn_t *n)
{
    NoiseDHState *dh;

    if (!n->hs) {
        return;
    }
    dh = noise_handshakestate_get_local_keypair_dh(n->hs);
    if (dh) {
        noise_dhstate_clear_key(dh);
    }
    dh = noise_handshakestate_get_fixed_ephemeral_dh(n->hs);
    if (dh) {
        noise_dhstate_clear_key(dh);
    }
    noise_handshakestate_free(n->hs);
    n->hs = NULL;
}

void sn_free(sn_t *n)
{
    if (!n) {
        return;
    }
    sn_drop_hs(n);
    noise_cipherstate_free(n->tx);
    noise_cipherstate_free(n->rx);
    free(n);
}

static int sn_set_keys(sn_t *n, const sn_opts *o)
{
    NoiseDHState *dh = noise_handshakestate_get_local_keypair_dh(n->hs);

    if (!dh || noise_dhstate_set_keypair_private(dh, o->static_priv, SESS_KEY_LEN) != NOISE_ERROR_NONE) {
        return -1;
    }
    if (o->eph_priv) {
        dh = noise_handshakestate_get_fixed_ephemeral_dh(n->hs);
        if (!dh || noise_dhstate_set_keypair_private(dh, o->eph_priv, SESS_KEY_LEN) != NOISE_ERROR_NONE) {
            return -1;
        }
    }
    return 0;
}

sn_t *sn_new(sess_role role, const sn_opts *o)
{
    sn_t *n;
    int nr = (role == SESS_INITIATOR) ? NOISE_ROLE_INITIATOR : NOISE_ROLE_RESPONDER;

    if (!o || !o->static_priv) {
        return NULL;
    }
    n = calloc(1, sizeof(*n));
    if (!n) {
        return NULL;
    }
    if (noise_handshakestate_new_by_name(&n->hs, SN_PROTO_NAME, nr) != NOISE_ERROR_NONE) {
        free(n);
        return NULL;
    }
    if (sn_set_keys(n, o) != 0
        || noise_handshakestate_set_prologue(n->hs, o->prologue, o->prologue_len) != NOISE_ERROR_NONE
        || noise_handshakestate_start(n->hs) != NOISE_ERROR_NONE) {
        sn_free(n);
        return NULL;
    }
    return n;
}

static int sn_map_err(int err)
{
    return (err == NOISE_ERROR_MAC_FAILURE) ? SESS_EDECRYPT : SESS_EPROTO;
}

int sn_write_hs(sn_t *n, const void *payload, size_t plen, uint8_t *out, size_t cap, size_t *out_len)
{
    NoiseBuffer mbuf;
    NoiseBuffer pbuf;
    int err;

    if (!n || !n->hs || !out || !out_len) {
        return SESS_EPROTO;
    }
    /* noise-c lit le payload en écriture sans le modifier ; le const est retiré pour son API. */
    noise_buffer_set_output(mbuf, out, cap);
    noise_buffer_set_input(pbuf, payload ? (uint8_t *)payload : (uint8_t *)NO_PL, plen);
    err = noise_handshakestate_write_message(n->hs, &mbuf, &pbuf);
    if (err != NOISE_ERROR_NONE) {
        return sn_map_err(err);
    }
    *out_len = mbuf.size;
    return 0;
}

int sn_read_hs(sn_t *n, const uint8_t *in, size_t in_len, uint8_t *payload, size_t cap, size_t *plen)
{
    uint8_t tmp[SN_HS_MAX];
    NoiseBuffer mbuf;
    NoiseBuffer pbuf;
    int err;

    if (!n || !n->hs || !in || !plen || in_len == 0 || in_len > sizeof(tmp)) {
        return SESS_EPROTO;
    }
    /* noise-c déchiffre en place : travail sur une copie. */
    memcpy(tmp, in, in_len);
    noise_buffer_set_inout(mbuf, tmp, in_len, in_len);
    noise_buffer_set_output(pbuf, payload, cap);
    err = noise_handshakestate_read_message(n->hs, &mbuf, &pbuf);
    *plen = pbuf.size;
    explicit_bzero(tmp, sizeof(tmp));
    return err == NOISE_ERROR_NONE ? 0 : sn_map_err(err);
}

int sn_remote_static(const sn_t *n, uint8_t out[SESS_KEY_LEN])
{
    NoiseDHState *dh;

    if (!n || !n->hs || !noise_handshakestate_has_remote_public_key(n->hs)) {
        return -1;
    }
    dh = noise_handshakestate_get_remote_public_key_dh(n->hs);
    if (!dh || noise_dhstate_get_public_key(dh, out, SESS_KEY_LEN) != NOISE_ERROR_NONE) {
        return -1;
    }
    return 0;
}

int sn_hash(const sn_t *n, uint8_t *out, size_t len)
{
    if (!n || !n->hs || !out) {
        return -1;
    }
    return noise_handshakestate_get_handshake_hash(n->hs, out, len) == NOISE_ERROR_NONE ? 0 : -1;
}

int sn_split(sn_t *n)
{
    if (!n || !n->hs || noise_handshakestate_get_action(n->hs) != NOISE_ACTION_SPLIT) {
        return SESS_EPROTO;
    }
    if (noise_handshakestate_split(n->hs, &n->tx, &n->rx) != NOISE_ERROR_NONE) {
        return SESS_EPROTO;
    }
    sn_drop_hs(n);
    return 0;
}

/* Rekey Noise : k' = 32 premiers octets de ENCRYPT(k, nonce, zeros(32)). noise-c n'expose pas
 * noise_cipherstate_rekey ; le nonce de rekey est 2^64-2 au lieu de 2^64-1 (refusé par noise-c). */
static int sn_rekey(NoiseCipherState *cs, uint64_t count)
{
    uint8_t buf[SESS_KEY_LEN + SN_MAC_LEN] = {0};
    NoiseBuffer b;
    int ok = 0;

    if (noise_cipherstate_set_nonce(cs, SN_REKEY_NONCE) == NOISE_ERROR_NONE) {
        noise_buffer_set_inout(b, buf, SESS_KEY_LEN, sizeof(buf));
        if (noise_cipherstate_encrypt(cs, &b) == NOISE_ERROR_NONE
            && noise_cipherstate_init_key(cs, buf, SESS_KEY_LEN) == NOISE_ERROR_NONE
            && noise_cipherstate_set_nonce(cs, count) == NOISE_ERROR_NONE) {
            ok = 1;
        }
    }
    explicit_bzero(buf, sizeof(buf));
    return ok ? 0 : SESS_EPROTO;
}

static int sn_rekey_if_due(NoiseCipherState *cs, uint64_t count)
{
    if (count == 0 || count % SESS_REKEY_EVERY != 0) {
        return 0;
    }
    return sn_rekey(cs, count);
}

int sn_encrypt(sn_t *n, const uint8_t *pt, size_t len, uint8_t *ct)
{
    NoiseBuffer b;
    int err;

    if (!n || !n->tx || !ct || (len > 0 && !pt)) {
        return SESS_EPROTO;
    }
    if (sn_rekey_if_due(n->tx, n->tx_count) != 0) {
        return SESS_EPROTO;
    }
    if (len > 0) {
        memcpy(ct, pt, len);
    }
    noise_buffer_set_inout(b, ct, len, len + SN_MAC_LEN);
    err = noise_cipherstate_encrypt(n->tx, &b);
    if (err != NOISE_ERROR_NONE) {
        return SESS_EPROTO;
    }
    n->tx_count++;
    return 0;
}

int sn_decrypt(sn_t *n, const uint8_t *ct, size_t len, uint8_t *pt)
{
    NoiseBuffer b;

    if (!n || !n->rx || !pt || !ct || len < SN_MAC_LEN) {
        return SESS_EPROTO;
    }
    if (sn_rekey_if_due(n->rx, n->rx_count) != 0) {
        return SESS_EPROTO;
    }
    uint8_t *work = malloc(len);
    if (!work) {
        return SESS_EPROTO;
    }
    memcpy(work, ct, len);
    noise_buffer_set_inout(b, work, len, len);
    int err = noise_cipherstate_decrypt(n->rx, &b);
    if (err == NOISE_ERROR_NONE) {
        memcpy(pt, work, len - SN_MAC_LEN);
        n->rx_count++;
    }
    explicit_bzero(work, len);
    free(work);
    return err == NOISE_ERROR_NONE ? 0 : SESS_EDECRYPT;
}
