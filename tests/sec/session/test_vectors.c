#define _GNU_SOURCE
/* Vecteurs Noise XX (noise-c) rejoués via la couche interne sess_noise : handshake + transport octet à octet. */
#include <string.h>
#include "test.h"
#include "vectors_xx.h"
#include "sec/session/sess_noise.h"

#define HASH_MAX 64u
#define BUF_MAX  256u

static void make_pair(const xx_vector *v, sn_t **ini, sn_t **resp)
{
    sn_opts oi = { v->init_static, v->init_ephemeral, v->init_prologue, v->init_prologue_len };
    sn_opts opr = { v->resp_static, v->resp_ephemeral, v->resp_prologue, v->resp_prologue_len };
    *ini = sn_new(SESS_INITIATOR, &oi);
    *resp = sn_new(SESS_RESPONDER, &opr);
    CHECK(*ini != NULL);
    CHECK(*resp != NULL);
}

static void hs_step(sn_t *from, sn_t *to, const xx_vector_msg *m)
{
    uint8_t ct[BUF_MAX];
    uint8_t pl[BUF_MAX];
    size_t ct_len = 0;
    size_t pl_len = 0;

    CHECK_EQ_INT(sn_write_hs(from, m->payload, m->payload_len, ct, sizeof(ct), &ct_len), 0);
    CHECK_EQ_INT(ct_len, m->ciphertext_len);
    if (ct_len == m->ciphertext_len) {
        CHECK_EQ_MEM(ct, m->ciphertext, ct_len);
    }
    CHECK_EQ_INT(sn_read_hs(to, ct, ct_len, pl, sizeof(pl), &pl_len), 0);
    CHECK_EQ_INT(pl_len, m->payload_len);
    if (pl_len == m->payload_len) {
        CHECK_EQ_MEM(pl, m->payload, pl_len);
    }
}

static void check_handshake_hash(const xx_vector *v, sn_t *ini, sn_t *resp)
{
    uint8_t hi[HASH_MAX];
    uint8_t hr[HASH_MAX];
    size_t hlen = v->handshake_hash ? v->handshake_hash_len : HASH_MAX;

    CHECK_EQ_INT(sn_hash(ini, hi, hlen), 0);
    CHECK_EQ_INT(sn_hash(resp, hr, hlen), 0);
    CHECK_EQ_MEM(hi, hr, hlen);
    if (v->handshake_hash) {
        CHECK_EQ_MEM(hi, v->handshake_hash, hlen);
    }
}

static void transport_step(sn_t *from, sn_t *to, const xx_vector_msg *m)
{
    uint8_t ct[BUF_MAX];
    uint8_t pt[BUF_MAX];

    CHECK_EQ_INT(m->ciphertext_len, m->payload_len + SN_MAC_LEN);
    CHECK_EQ_INT(sn_encrypt(from, m->payload, m->payload_len, ct), 0);
    if (m->ciphertext_len == m->payload_len + SN_MAC_LEN) {
        CHECK_EQ_MEM(ct, m->ciphertext, m->ciphertext_len);
    }
    CHECK_EQ_INT(sn_decrypt(to, ct, m->ciphertext_len, pt), 0);
    if (m->payload_len > 0) {
        CHECK_EQ_MEM(pt, m->payload, m->payload_len);
    }
}

static void check_vector(const xx_vector *v)
{
    sn_t *ini = NULL;
    sn_t *resp = NULL;
    size_t i;

    make_pair(v, &ini, &resp);
    if (!ini || !resp) {
        sn_free(ini);
        sn_free(resp);
        return;
    }
    for (i = 0; i < 3; ++i) {
        sn_t *from = (i % 2 == 0) ? ini : resp;
        sn_t *to = (i % 2 == 0) ? resp : ini;
        hs_step(from, to, &v->msgs[i]);
    }
    check_handshake_hash(v, ini, resp);
    CHECK_EQ_INT(sn_split(ini), 0);
    CHECK_EQ_INT(sn_split(resp), 0);
    for (i = 3; i < v->msg_count; ++i) {
        sn_t *from = (i % 2 == 0) ? ini : resp;
        sn_t *to = (i % 2 == 0) ? resp : ini;
        transport_step(from, to, &v->msgs[i]);
    }
    sn_free(ini);
    sn_free(resp);
}

int main(void)
{
    size_t i;

    CHECK(XX_VECTOR_COUNT >= 1);
    for (i = 0; i < XX_VECTOR_COUNT; ++i) {
        check_vector(XX_VECTORS[i]);
    }
    return test_summary();
}
