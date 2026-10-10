/* proto/frame : trames [len u32 BE][type u8][payload], len = 1 + taille payload.
 * frame_parse ne consomme rien tant que la trame n'est pas complète et valide, n'alloue jamais.
 * frame_encode est atomique vis-à-vis de BUF_ENOSPC (vérifié avant écriture) ; un -ENOMEM en cours
 * d'écriture peut laisser l'en-tête seul dans out (l'appelant doit alors jeter out). */
#include "proto/frame/frame.h"

#include <errno.h>
#include <string.h>

#define FRAME_LEN_SIZE 4u
#define FRAME_LEN_MAX  (FRAME_MAX - FRAME_LEN_SIZE)

void frame_types_allow(frame_types *t, uint8_t type)
{
    if (t == NULL) {
        return;
    }
    t->bits[type >> 3] |= (uint8_t)(1u << (type & 7u));
}

static int frame_type_allowed(const frame_types *allowed, uint8_t type)
{
    if (allowed == NULL) {
        return 1;
    }
    return (allowed->bits[type >> 3] & (1u << (type & 7u))) != 0;
}

int frame_parse(buf_t *rx, const frame_types *allowed, frame_t *out)
{
    const uint8_t *p;
    uint32_t len;
    size_t readable;

    if (rx == NULL || out == NULL) {
        return -EINVAL;
    }
    readable = buf_readable(rx);
    if (readable < FRAME_LEN_SIZE) {
        return FRAME_EAGAIN;
    }
    p = buf_peek(rx);
    len = buf_load_u32be(p);
    if (len == 0) {
        return FRAME_EBADLEN;
    }
    if (len > FRAME_LEN_MAX) {
        return FRAME_ETOOBIG;
    }
    if (readable < FRAME_LEN_SIZE + 1) {
        return FRAME_EAGAIN;
    }
    if (!frame_type_allowed(allowed, p[FRAME_LEN_SIZE])) {
        return FRAME_EBADTYPE;
    }
    if (readable < FRAME_LEN_SIZE + (size_t)len) {
        return FRAME_EAGAIN;
    }
    out->type = p[FRAME_LEN_SIZE];
    out->payload = p + FRAME_HDR;
    out->len = (size_t)len - 1;
    buf_consume(rx, FRAME_LEN_SIZE + (size_t)len);
    return 0;
}

int frame_encode(buf_t *out, uint8_t type, const void *payload, size_t len)
{
    uint8_t hdr[FRAME_HDR];
    int rc;

    if (out == NULL) {
        return -EINVAL;
    }
    if (len > FRAME_PAYLOAD_MAX) {
        return FRAME_ETOOBIG;
    }
    if (FRAME_HDR + len > out->cap_max - buf_readable(out)) {
        return BUF_ENOSPC;
    }
    buf_store_u32be(hdr, (uint32_t)len + 1u);
    hdr[FRAME_LEN_SIZE] = type;
    rc = buf_append(out, hdr, FRAME_HDR);
    if (rc != 0) {
        return rc;
    }
    return buf_append(out, payload, len);
}
