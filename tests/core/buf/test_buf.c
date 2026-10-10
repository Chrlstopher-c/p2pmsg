/* Tests unitaires de core/buf : chaque fonction de buf.h, bornes exactes, codage big-endian. */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "core/buf/buf.h"
#include "test.h"

static void test_init_and_free(void)
{
    buf_t b;

    CHECK_EQ_INT(buf_init(&b, 10), 0);
    CHECK(b.data == NULL);
    CHECK_EQ_INT(b.cap, 0);
    CHECK_EQ_INT(b.cap_max, 10);
    CHECK_EQ_INT(buf_readable(&b), 0);
    CHECK(buf_peek(&b) == NULL);
    buf_free(&b);
    buf_free(&b);
    CHECK(b.data == NULL);
    CHECK_EQ_INT(b.cap, 0);
    buf_free(NULL);
    CHECK_EQ_INT(buf_init(NULL, 10), -EINVAL);
}

static void test_append_exact_cap_max(void)
{
    buf_t b;
    uint8_t in[16];

    memset(in, 0xAB, sizeof in);
    CHECK_EQ_INT(buf_init(&b, 10), 0);
    CHECK_EQ_INT(buf_append(&b, in, 10), 0);
    CHECK_EQ_INT(buf_readable(&b), 10);
    CHECK_EQ_INT(b.cap, 10);
    CHECK_EQ_INT(buf_append(&b, in, 1), BUF_ENOSPC);
    CHECK_EQ_INT(buf_readable(&b), 10);
    buf_consume(&b, 3);
    CHECK_EQ_INT(buf_append(&b, in, 3), 0);
    CHECK_EQ_INT(buf_readable(&b), 10);
    CHECK_EQ_INT(buf_append(&b, in, 1), BUF_ENOSPC);
    buf_free(&b);
}

static void test_append_zero_and_cap_zero(void)
{
    buf_t b;
    uint8_t in[1] = {1};

    CHECK_EQ_INT(buf_init(&b, 0), 0);
    CHECK_EQ_INT(buf_append(&b, in, 0), 0);
    CHECK(b.data == NULL);
    CHECK_EQ_INT(buf_append(&b, in, 1), BUF_ENOSPC);
    CHECK(b.data == NULL);
    CHECK_EQ_INT(buf_readable(&b), 0);
    buf_free(&b);
}

static void test_append_huge_n_no_ub(void)
{
    buf_t b;
    uint8_t in[4] = {1, 2, 3, 4};

    CHECK_EQ_INT(buf_init(&b, 1U << 20), 0);
    CHECK_EQ_INT(buf_append(&b, in, 2), 0);
    CHECK_EQ_INT(buf_append(&b, in, SIZE_MAX), BUF_ENOSPC);
    CHECK_EQ_INT(buf_append(&b, in, SIZE_MAX - 1), BUF_ENOSPC);
    CHECK_EQ_INT(buf_readable(&b), 2);
    buf_free(&b);
}

static void test_growth_doubles_and_is_bounded(void)
{
    buf_t b;
    uint8_t in[128];

    memset(in, 0x11, sizeof in);
    CHECK_EQ_INT(buf_init(&b, 100), 0);
    CHECK_EQ_INT(buf_append(&b, in, 1), 0);
    CHECK_EQ_INT(b.cap, 64);
    CHECK_EQ_INT(buf_append(&b, in, 64), 0);
    CHECK_EQ_INT(b.cap, 100);
    CHECK_EQ_INT(buf_append(&b, in, 35), 0);
    CHECK_EQ_INT(b.cap, 100);
    CHECK_EQ_INT(buf_append(&b, in, 1), BUF_ENOSPC);
    buf_free(&b);
}

static void test_compact_on_append_keeps_order(void)
{
    buf_t b;
    uint8_t first[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t second[6] = {9, 10, 11, 12, 13, 14};
    uint8_t expect[8] = {7, 8, 9, 10, 11, 12, 13, 14};

    CHECK_EQ_INT(buf_init(&b, 10), 0);
    CHECK_EQ_INT(buf_append(&b, first, 8), 0);
    buf_consume(&b, 6);
    CHECK_EQ_INT(buf_append(&b, second, 6), 0);
    CHECK_EQ_INT(buf_readable(&b), 8);
    CHECK(buf_peek(&b) != NULL);
    CHECK_EQ_MEM(buf_peek(&b), expect, 8);
    buf_free(&b);
}

static void test_consume_clamps_and_peek(void)
{
    buf_t b;
    uint8_t in[5] = {0, 1, 2, 3, 4};

    CHECK_EQ_INT(buf_init(&b, 16), 0);
    CHECK_EQ_INT(buf_append(&b, in, 5), 0);
    CHECK_EQ_MEM(buf_peek(&b), in, 5);
    buf_consume(&b, 2);
    CHECK_EQ_INT(buf_readable(&b), 3);
    CHECK_EQ_MEM(buf_peek(&b), in + 2, 3);
    buf_consume(&b, 100);
    CHECK_EQ_INT(buf_readable(&b), 0);
    CHECK(buf_peek(&b) == NULL);
    buf_compact(&b);
    CHECK_EQ_INT(b.len, 0);
    CHECK_EQ_INT(b.rpos, 0);
    buf_free(&b);
}

static void test_compact_moves_unread_to_front(void)
{
    buf_t b;
    uint8_t in[4] = {'a', 'b', 'c', 'd'};

    CHECK_EQ_INT(buf_init(&b, 16), 0);
    CHECK_EQ_INT(buf_append(&b, in, 4), 0);
    buf_consume(&b, 1);
    buf_compact(&b);
    CHECK_EQ_INT(b.rpos, 0);
    CHECK_EQ_INT(b.len, 3);
    CHECK_EQ_MEM(b.data, "bcd", 3);
    buf_compact(&b);
    CHECK_EQ_INT(buf_readable(&b), 3);
    buf_free(&b);
}

static void test_reset_keeps_allocation(void)
{
    buf_t b;
    uint8_t in[3] = {1, 2, 3};

    CHECK_EQ_INT(buf_init(&b, 16), 0);
    CHECK_EQ_INT(buf_append(&b, in, 3), 0);
    buf_reset(&b);
    CHECK_EQ_INT(b.len, 0);
    CHECK_EQ_INT(b.rpos, 0);
    CHECK_EQ_INT(b.cap, 16);
    CHECK(b.data != NULL);
    buf_free(&b);
}

static void test_get_short_consumes_nothing(void)
{
    buf_t b;
    uint8_t in[3] = {0xDE, 0xAD, 0xBE};
    uint8_t one = 0x55;
    uint32_t wide = 0x77777777U;
    uint8_t dst[4] = {0};

    CHECK_EQ_INT(buf_init(&b, 16), 0);
    CHECK_EQ_INT(buf_get_u32be(&b, &wide), BUF_ESHORT);
    CHECK_EQ_INT(wide, 0x77777777U);
    CHECK_EQ_INT(buf_get_u8(&b, &one), BUF_ESHORT);
    CHECK_EQ_INT(one, 0x55);
    CHECK_EQ_INT(buf_append(&b, in, 3), 0);
    CHECK_EQ_INT(buf_get_bytes(&b, dst, 4), BUF_ESHORT);
    CHECK_EQ_INT(buf_readable(&b), 3);
    CHECK_EQ_INT(buf_get_bytes(&b, dst, 3), 0);
    CHECK_EQ_MEM(dst, in, 3);
    CHECK_EQ_INT(buf_readable(&b), 0);
    CHECK_EQ_INT(buf_get_bytes(&b, dst, 0), 0);
    buf_free(&b);
}

static void test_put_get_u8(void)
{
    buf_t b;
    uint8_t v = 0;

    CHECK_EQ_INT(buf_init(&b, 2), 0);
    CHECK_EQ_INT(buf_put_u8(&b, 0x7F), 0);
    CHECK_EQ_INT(buf_put_u8(&b, 0x80), 0);
    CHECK_EQ_INT(buf_put_u8(&b, 0x01), BUF_ENOSPC);
    CHECK_EQ_INT(buf_get_u8(&b, &v), 0);
    CHECK_EQ_INT(v, 0x7F);
    CHECK_EQ_INT(buf_get_u8(&b, &v), 0);
    CHECK_EQ_INT(v, 0x80);
    CHECK_EQ_INT(buf_get_u8(&b, &v), BUF_ESHORT);
    buf_free(&b);
}

static void test_u32be_bytes_and_bounds(void)
{
    buf_t b;
    uint32_t v = 0;
    const uint8_t expect[4] = {0x01, 0x02, 0x03, 0x04};

    CHECK_EQ_INT(buf_init(&b, 4), 0);
    CHECK_EQ_INT(buf_put_u32be(&b, 0x01020304U), 0);
    CHECK_EQ_MEM(buf_peek(&b), expect, 4);
    CHECK_EQ_INT(buf_get_u32be(&b, &v), 0);
    CHECK_EQ_INT(v, 0x01020304U);
    CHECK_EQ_INT(buf_put_u32be(&b, 0), 0);
    CHECK_EQ_INT(buf_put_u8(&b, 1), BUF_ENOSPC);
    buf_free(&b);
    CHECK_EQ_INT(buf_init(&b, 3), 0);
    CHECK_EQ_INT(buf_put_u32be(&b, 1), BUF_ENOSPC);
    CHECK_EQ_INT(buf_readable(&b), 0);
    buf_free(&b);
}

static void test_u64be_bytes_and_roundtrip(void)
{
    buf_t b;
    uint64_t v = 0;
    const uint8_t expect[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    CHECK_EQ_INT(buf_init(&b, 16), 0);
    CHECK_EQ_INT(buf_put_u64be(&b, 0x0102030405060708ULL), 0);
    CHECK_EQ_MEM(buf_peek(&b), expect, 8);
    CHECK_EQ_INT(buf_get_u64be(&b, &v), 0);
    CHECK(v == 0x0102030405060708ULL);
    CHECK_EQ_INT(buf_get_u64be(&b, &v), BUF_ESHORT);
    buf_free(&b);
}

static void test_store_load_be(void)
{
    uint8_t p[8];
    const uint8_t e32[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    const uint8_t e64[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};

    buf_store_u32be(p, 0xDEADBEEFU);
    CHECK_EQ_MEM(p, e32, 4);
    CHECK_EQ_INT(buf_load_u32be(p), 0xDEADBEEFU);
    buf_store_u64be(p, 0x0011223344556677ULL);
    CHECK_EQ_MEM(p, e64, 8);
    CHECK(buf_load_u64be(p) == 0x0011223344556677ULL);
}

static void test_get_bytes_partial_then_rest(void)
{
    buf_t b;
    uint8_t in[6] = {1, 2, 3, 4, 5, 6};
    uint8_t out[6] = {0};

    CHECK_EQ_INT(buf_init(&b, 16), 0);
    CHECK_EQ_INT(buf_append(&b, in, 6), 0);
    CHECK_EQ_INT(buf_get_bytes(&b, out, 2), 0);
    CHECK_EQ_MEM(out, in, 2);
    CHECK_EQ_INT(buf_readable(&b), 4);
    CHECK_EQ_INT(buf_get_bytes(&b, out + 2, 4), 0);
    CHECK_EQ_MEM(out, in, 6);
    buf_free(&b);
}

int main(void)
{
    RUN(test_init_and_free);
    RUN(test_append_exact_cap_max);
    RUN(test_append_zero_and_cap_zero);
    RUN(test_append_huge_n_no_ub);
    RUN(test_growth_doubles_and_is_bounded);
    RUN(test_compact_on_append_keeps_order);
    RUN(test_consume_clamps_and_peek);
    RUN(test_compact_moves_unread_to_front);
    RUN(test_reset_keeps_allocation);
    RUN(test_get_short_consumes_nothing);
    RUN(test_put_get_u8);
    RUN(test_u32be_bytes_and_bounds);
    RUN(test_u64be_bytes_and_roundtrip);
    RUN(test_store_load_be);
    RUN(test_get_bytes_partial_then_rest);
    return test_summary();
}
