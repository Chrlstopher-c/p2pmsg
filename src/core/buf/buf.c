/* core/buf : buffer d'octets borné. Allocation paresseuse, croissance ×2 plafonnée par cap_max,
 * compaction avant croissance. Capacité initiale = min(64, cap_max). Argument NULL -> -EINVAL. */
#include "core/buf/buf.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define BUF_INITIAL_CAP 64

static size_t buf_grow_target(const buf_t *b, size_t needed)
{
    size_t target;

    if (b->cap == 0) {
        target = BUF_INITIAL_CAP;
    } else if (b->cap > b->cap_max / 2) {
        target = b->cap_max;
    } else {
        target = b->cap * 2;
    }
    if (target < needed) {
        target = needed;
    }
    if (target > b->cap_max) {
        target = b->cap_max;
    }
    return target;
}

static int buf_grow(buf_t *b, size_t needed)
{
    size_t target = buf_grow_target(b, needed);
    uint8_t *p = realloc(b->data, target);

    if (p == NULL) {
        return -ENOMEM;
    }
    b->data = p;
    b->cap = target;
    return 0;
}

int buf_init(buf_t *b, size_t cap_max)
{
    if (b == NULL) {
        return -EINVAL;
    }
    b->data = NULL;
    b->len = 0;
    b->rpos = 0;
    b->cap = 0;
    b->cap_max = cap_max;
    return 0;
}

void buf_free(buf_t *b)
{
    if (b == NULL) {
        return;
    }
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->rpos = 0;
    b->cap = 0;
}

void buf_reset(buf_t *b)
{
    if (b == NULL) {
        return;
    }
    b->len = 0;
    b->rpos = 0;
}

size_t buf_readable(const buf_t *b)
{
    if (b == NULL) {
        return 0;
    }
    return b->len - b->rpos;
}

const uint8_t *buf_peek(const buf_t *b)
{
    if (b == NULL || buf_readable(b) == 0) {
        return NULL;
    }
    return b->data + b->rpos;
}

void buf_consume(buf_t *b, size_t n)
{
    size_t readable;

    if (b == NULL) {
        return;
    }
    readable = buf_readable(b);
    b->rpos += n < readable ? n : readable;
}

void buf_compact(buf_t *b)
{
    size_t readable;

    if (b == NULL || b->rpos == 0) {
        return;
    }
    readable = buf_readable(b);
    if (readable > 0) {
        memmove(b->data, b->data + b->rpos, readable);
    }
    b->len = readable;
    b->rpos = 0;
}

int buf_append(buf_t *b, const void *src, size_t n)
{
    size_t readable;
    int rc;

    if (b == NULL || (src == NULL && n > 0)) {
        return -EINVAL;
    }
    if (n == 0) {
        return 0;
    }
    readable = buf_readable(b);
    if (n > b->cap_max - readable) {
        return BUF_ENOSPC;
    }
    if (b->len + n > b->cap) {
        if (readable + n > b->cap) {
            rc = buf_grow(b, readable + n);
            if (rc != 0) {
                return rc;
            }
        }
        buf_compact(b);
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return 0;
}

int buf_put_u8(buf_t *b, uint8_t v)
{
    return buf_append(b, &v, 1);
}

int buf_put_u32be(buf_t *b, uint32_t v)
{
    uint8_t enc[4];

    buf_store_u32be(enc, v);
    return buf_append(b, enc, sizeof enc);
}

int buf_put_u64be(buf_t *b, uint64_t v)
{
    uint8_t enc[8];

    buf_store_u64be(enc, v);
    return buf_append(b, enc, sizeof enc);
}

int buf_get_bytes(buf_t *b, void *dst, size_t n)
{
    if (b == NULL || (dst == NULL && n > 0)) {
        return -EINVAL;
    }
    if (n > buf_readable(b)) {
        return BUF_ESHORT;
    }
    if (n > 0) {
        memcpy(dst, b->data + b->rpos, n);
        b->rpos += n;
    }
    return 0;
}

int buf_get_u8(buf_t *b, uint8_t *out)
{
    if (out == NULL) {
        return -EINVAL;
    }
    return buf_get_bytes(b, out, 1);
}

int buf_get_u32be(buf_t *b, uint32_t *out)
{
    uint8_t enc[4];
    int rc;

    if (out == NULL) {
        return -EINVAL;
    }
    rc = buf_get_bytes(b, enc, sizeof enc);
    if (rc == 0) {
        *out = buf_load_u32be(enc);
    }
    return rc;
}

int buf_get_u64be(buf_t *b, uint64_t *out)
{
    uint8_t enc[8];
    int rc;

    if (out == NULL) {
        return -EINVAL;
    }
    rc = buf_get_bytes(b, enc, sizeof enc);
    if (rc == 0) {
        *out = buf_load_u64be(enc);
    }
    return rc;
}

uint32_t buf_load_u32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint64_t buf_load_u64be(const uint8_t *p)
{
    return ((uint64_t)buf_load_u32be(p) << 32) | (uint64_t)buf_load_u32be(p + 4);
}

void buf_store_u32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void buf_store_u64be(uint8_t *p, uint64_t v)
{
    buf_store_u32be(p, (uint32_t)(v >> 32));
    buf_store_u32be(p + 4, (uint32_t)v);
}
