/* Test de modèle : 10000 opérations aléatoires (seed fixe) sur buf_t comparées à un modèle naïf. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/buf/buf.h"
#include "test.h"

#define MODEL_CAP 256
#define MODEL_OPS 10000
#define MODEL_SEED 0x2545F4914F6CDD1DULL

typedef struct {
    uint8_t bytes[MODEL_CAP];
    size_t len;
} model_t;

static uint64_t rng_next(uint64_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

static size_t rng_below(uint64_t *s, size_t m)
{
    return (size_t)(rng_next(s) % m);
}

static void model_push(model_t *m, const uint8_t *src, size_t n)
{
    memcpy(m->bytes + m->len, src, n);
    m->len += n;
}

static void model_drop(model_t *m, size_t n)
{
    memmove(m->bytes, m->bytes + n, m->len - n);
    m->len -= n;
}

static uint32_t model_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t model_be64(const uint8_t *p)
{
    return ((uint64_t)model_be32(p) << 32) | (uint64_t)model_be32(p + 4);
}

static void op_append(buf_t *b, model_t *m, uint64_t *s)
{
    uint8_t src[40];
    size_t n = rng_below(s, sizeof src + 1);
    int want = m->len + n > MODEL_CAP ? BUF_ENOSPC : 0;

    for (size_t i = 0; i < n; i++) {
        src[i] = (uint8_t)rng_next(s);
    }
    CHECK_EQ_INT(buf_append(b, src, n), want);
    if (want == 0) {
        model_push(m, src, n);
    }
}

static void op_put_u32(buf_t *b, model_t *m, uint64_t *s)
{
    uint32_t v = (uint32_t)rng_next(s);
    uint8_t e[4];
    int want = m->len + 4 > MODEL_CAP ? BUF_ENOSPC : 0;

    buf_store_u32be(e, v);
    CHECK_EQ_INT(buf_put_u32be(b, v), want);
    if (want == 0) {
        model_push(m, e, 4);
    }
}

static void op_put_u64(buf_t *b, model_t *m, uint64_t *s)
{
    uint64_t v = rng_next(s);
    uint8_t e[8];
    int want = m->len + 8 > MODEL_CAP ? BUF_ENOSPC : 0;

    buf_store_u64be(e, v);
    CHECK_EQ_INT(buf_put_u64be(b, v), want);
    if (want == 0) {
        model_push(m, e, 8);
    }
}

static void op_get_u32(buf_t *b, model_t *m)
{
    uint32_t v = 0;
    int rc = buf_get_u32be(b, &v);

    if (m->len < 4) {
        CHECK_EQ_INT(rc, BUF_ESHORT);
        return;
    }
    CHECK_EQ_INT(rc, 0);
    CHECK_EQ_INT(v, model_be32(m->bytes));
    model_drop(m, 4);
}

static void op_get_u64(buf_t *b, model_t *m)
{
    uint64_t v = 0;
    int rc = buf_get_u64be(b, &v);

    if (m->len < 8) {
        CHECK_EQ_INT(rc, BUF_ESHORT);
        return;
    }
    CHECK_EQ_INT(rc, 0);
    CHECK(v == model_be64(m->bytes));
    model_drop(m, 8);
}

static void op_get_bytes(buf_t *b, model_t *m, uint64_t *s)
{
    uint8_t dst[20];
    size_t n = rng_below(s, sizeof dst + 1);
    int rc = buf_get_bytes(b, dst, n);

    if (n > m->len) {
        CHECK_EQ_INT(rc, BUF_ESHORT);
        return;
    }
    CHECK_EQ_INT(rc, 0);
    CHECK_EQ_MEM(dst, m->bytes, n);
    model_drop(m, n);
}

static void op_consume(buf_t *b, model_t *m, uint64_t *s)
{
    size_t n = rng_below(s, 51);
    size_t k = n < m->len ? n : m->len;

    buf_consume(b, n);
    model_drop(m, k);
}

static void check_state(const buf_t *b, const model_t *m)
{
    CHECK(b->rpos <= b->len && b->len <= b->cap && b->cap <= b->cap_max);
    CHECK_EQ_INT(buf_readable(b), m->len);
    if (m->len == 0) {
        CHECK(buf_peek(b) == NULL);
    } else {
        CHECK_EQ_MEM(buf_peek(b), m->bytes, m->len);
    }
}

static void step(buf_t *b, model_t *m, uint64_t *s)
{
    switch (rng_below(s, 11)) {
    case 0: case 1: op_append(b, m, s); break;
    case 2: op_consume(b, m, s); break;
    case 3: buf_compact(b); break;
    case 4: op_put_u32(b, m, s); break;
    case 5: op_put_u64(b, m, s); break;
    case 6: op_get_u32(b, m); break;
    case 7: op_get_u64(b, m); break;
    case 8: op_get_bytes(b, m, s); break;
    case 9: buf_reset(b); m->len = 0; break;
    default: op_append(b, m, s); break;
    }
}

static void test_random_ops_match_model(void)
{
    buf_t b;
    model_t m;
    uint64_t s = MODEL_SEED;

    memset(&m, 0, sizeof m);
    CHECK_EQ_INT(buf_init(&b, MODEL_CAP), 0);
    for (int i = 0; i < MODEL_OPS; i++) {
        step(&b, &m, &s);
        check_state(&b, &m);
    }
    buf_free(&b);
}

int main(void)
{
    RUN(test_random_ops_match_model);
    return test_summary();
}
