/* Cible libFuzzer de proto/frame (critère F7). Découpe l'entrée en morceaux de taille dérivée des
 * données, alimente un buf_t, boucle frame_parse jusqu'à EAGAIN/erreur, re-encode chaque trame
 * et vérifie l'égalité octet à octet avec le flux source. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core/buf/buf.h"
#include "proto/frame/frame.h"

#define FUZZ_CHUNK_MAX 64u
#define FUZZ_MSG_TYPE_LO 0x10u
#define FUZZ_MSG_TYPE_HI 0x15u

static void fuzz_check(int cond)
{
    if (!cond) {
        abort();
    }
}

static void fuzz_allowed(frame_types *t, uint8_t selector)
{
    uint8_t type;

    memset(t, 0, sizeof *t);
    for (type = FUZZ_MSG_TYPE_LO; type <= FUZZ_MSG_TYPE_HI; type++) {
        frame_types_allow(t, type);
    }
    frame_types_allow(t, selector);
}

static void fuzz_round_trip(buf_t *enc, const frame_t *f, const uint8_t *orig, size_t orig_len)
{
    int rc;

    buf_reset(enc);
    rc = frame_encode(enc, f->type, f->payload, f->len);
    fuzz_check(rc == 0);
    fuzz_check(buf_readable(enc) == orig_len);
    fuzz_check(memcmp(buf_peek(enc), orig, orig_len) == 0);
}

static void fuzz_drain(buf_t *rx, buf_t *enc, const frame_types *allowed)
{
    frame_t f;
    const uint8_t *orig;
    size_t orig_len;
    int rc;

    for (;;) {
        rc = frame_parse(rx, allowed, &f);
        if (rc != 0) {
            fuzz_check(rc == FRAME_EAGAIN || rc == FRAME_ETOOBIG || rc == FRAME_EBADLEN
                       || rc == FRAME_EBADTYPE);
            return;
        }
        fuzz_check(f.len <= FRAME_PAYLOAD_MAX);
        orig = f.payload - FRAME_HDR;
        orig_len = FRAME_HDR + f.len;
        fuzz_round_trip(enc, &f, orig, orig_len);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    buf_t rx;
    buf_t enc;
    frame_types allowed;
    size_t pos = 0;
    size_t chunk;

    if (size == 0) {
        return 0;
    }
    fuzz_allowed(&allowed, data[0]);
    fuzz_check(buf_init(&rx, 2u * FRAME_MAX) == 0);
    fuzz_check(buf_init(&enc, FRAME_MAX) == 0);
    while (pos < size) {
        chunk = 1u + (data[pos] % FUZZ_CHUNK_MAX);
        pos++;
        if (chunk > size - pos) {
            chunk = size - pos;
        }
        fuzz_check(buf_append(&rx, data + pos, chunk) == 0);
        pos += chunk;
        fuzz_drain(&rx, &enc, (data[0] & 1u) ? &allowed : NULL);
    }
    buf_free(&enc);
    buf_free(&rx);
    return 0;
}
