/* Tests de proto/frame : erreurs et leur détection précoce, bornes exactes, flux octet par octet,
 * plusieurs trames en un append, round-trip encode/parse. */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "core/buf/buf.h"
#include "proto/frame/frame.h"
#include "test.h"

#define RX_CAP (2u * FRAME_MAX)

static uint8_t big_payload[FRAME_MAX];

static void rx_init(buf_t *b)
{
    CHECK_EQ_INT(buf_init(b, RX_CAP), 0);
}

static void put(buf_t *b, const void *src, size_t n)
{
    CHECK_EQ_INT(buf_append(b, src, n), 0);
}

static void fill(uint8_t *p, size_t n, uint8_t seed)
{
    size_t i;

    for (i = 0; i < n; i++) {
        p[i] = (uint8_t)(seed + i * 7u);
    }
}

static void test_eagain_no_consume(void)
{
    buf_t rx;
    frame_t f;
    const uint8_t wire[] = {0x00, 0x00, 0x00, 0x03, 0x11, 0xAA, 0xBB};
    size_t i;

    rx_init(&rx);
    for (i = 0; i < sizeof wire - 1; i++) {
        put(&rx, &wire[i], 1);
        CHECK_EQ_INT(frame_parse(&rx, NULL, &f), FRAME_EAGAIN);
        CHECK_EQ_INT(buf_readable(&rx), i + 1);
    }
    put(&rx, &wire[sizeof wire - 1], 1);
    CHECK_EQ_INT(frame_parse(&rx, NULL, &f), 0);
    CHECK_EQ_INT(f.type, 0x11);
    CHECK_EQ_INT(f.len, 2);
    CHECK_EQ_MEM(f.payload, wire + 5, 2);
    CHECK_EQ_INT(buf_readable(&rx), 0);
    buf_free(&rx);
}

static void test_len_zero(void)
{
    buf_t rx;
    frame_t f;
    const uint8_t wire[] = {0x00, 0x00, 0x00, 0x00};

    rx_init(&rx);
    put(&rx, wire, sizeof wire);
    CHECK_EQ_INT(frame_parse(&rx, NULL, &f), FRAME_EBADLEN);
    CHECK_EQ_INT(buf_readable(&rx), 4);
    buf_free(&rx);
}

static void test_too_big_from_header_only(void)
{
    buf_t rx;
    frame_t f;
    const uint8_t over[] = {0x00, 0x00, 0xFF, 0xFD}; /* len = FRAME_MAX - 3 : trame de FRAME_MAX + 1 */
    const uint8_t exact[] = {0x00, 0x00, 0xFF, 0xFC}; /* len = FRAME_MAX - 4 : trame de FRAME_MAX */

    rx_init(&rx);
    put(&rx, over, sizeof over);
    CHECK_EQ_INT(frame_parse(&rx, NULL, &f), FRAME_ETOOBIG);
    CHECK_EQ_INT(buf_readable(&rx), 4);
    buf_reset(&rx);
    put(&rx, exact, sizeof exact);
    CHECK_EQ_INT(frame_parse(&rx, NULL, &f), FRAME_EAGAIN);
    buf_free(&rx);
}

static void test_bad_type_at_fifth_byte(void)
{
    buf_t rx;
    frame_t f;
    frame_types t;
    const uint8_t head[] = {0x00, 0x00, 0x00, 0x01};

    memset(&t, 0, sizeof t);
    frame_types_allow(&t, 0x10);
    rx_init(&rx);
    put(&rx, head, sizeof head);
    CHECK_EQ_INT(frame_parse(&rx, &t, &f), FRAME_EAGAIN);
    put(&rx, "\x99", 1);
    CHECK_EQ_INT(frame_parse(&rx, &t, &f), FRAME_EBADTYPE);
    CHECK_EQ_INT(buf_readable(&rx), 5);
    buf_free(&rx);
}

static void test_types_mask(void)
{
    buf_t rx;
    frame_t f;
    frame_types t;
    const uint8_t frm[] = {0x00, 0x00, 0x00, 0x01, 0x00};

    memset(&t, 0, sizeof t);
    frame_types_allow(&t, 0x10);
    frame_types_allow(&t, 0xFF);
    rx_init(&rx);
    put(&rx, frm, sizeof frm);
    CHECK_EQ_INT(frame_parse(&rx, &t, &f), FRAME_EBADTYPE);
    buf_free(&rx);

    rx_init(&rx);
    put(&rx, frm, sizeof frm);
    CHECK_EQ_INT(frame_parse(&rx, NULL, &f), 0);
    CHECK_EQ_INT(f.type, 0x00);
    CHECK_EQ_INT(f.len, 0);
    buf_free(&rx);
}

static void test_exact_max_frame(void)
{
    buf_t tx, rx;
    frame_t f;

    fill(big_payload, FRAME_PAYLOAD_MAX, 3);
    CHECK_EQ_INT(buf_init(&tx, RX_CAP), 0);
    rx_init(&rx);
    CHECK_EQ_INT(frame_encode(&tx, 0x11, big_payload, FRAME_PAYLOAD_MAX), 0);
    CHECK_EQ_INT(buf_readable(&tx), FRAME_MAX);
    put(&rx, buf_peek(&tx), buf_readable(&tx));
    CHECK_EQ_INT(frame_parse(&rx, NULL, &f), 0);
    CHECK_EQ_INT(f.len, FRAME_PAYLOAD_MAX);
    CHECK_EQ_MEM(f.payload, big_payload, FRAME_PAYLOAD_MAX);
    CHECK_EQ_INT(buf_readable(&rx), 0);
    buf_free(&rx);
    buf_free(&tx);
}

static void test_one_over_max_refused(void)
{
    buf_t tx;

    fill(big_payload, FRAME_MAX, 9);
    CHECK_EQ_INT(buf_init(&tx, RX_CAP), 0);
    CHECK_EQ_INT(frame_encode(&tx, 0x11, big_payload, FRAME_PAYLOAD_MAX + 1), FRAME_ETOOBIG);
    CHECK_EQ_INT(buf_readable(&tx), 0);
    buf_free(&tx);
}

static size_t build_stream(buf_t *tx)
{
    static const uint8_t hello[] = "hello";
    static const uint8_t bye[] = "bye";

    CHECK_EQ_INT(buf_init(tx, RX_CAP), 0);
    CHECK_EQ_INT(frame_encode(tx, 0x13, NULL, 0), 0);
    CHECK_EQ_INT(frame_encode(tx, 0x11, hello, sizeof hello - 1), 0);
    CHECK_EQ_INT(frame_encode(tx, 0x15, bye, sizeof bye - 1), 0);
    return buf_readable(tx);
}

static void check_stream_frame(const frame_t *f, size_t idx)
{
    static const uint8_t types[] = {0x13, 0x11, 0x15};
    static const size_t lens[] = {0, 5, 3};

    CHECK(idx < 3);
    if (idx >= 3) {
        return;
    }
    CHECK_EQ_INT(f->type, types[idx]);
    CHECK_EQ_INT(f->len, lens[idx]);
    if (idx == 1) {
        CHECK_EQ_MEM(f->payload, "hello", 5);
    } else if (idx == 2) {
        CHECK_EQ_MEM(f->payload, "bye", 3);
    }
}

static void test_byte_by_byte_stream(void)
{
    buf_t tx, rx;
    frame_t f;
    size_t n, i, got = 0;
    int rc;

    n = build_stream(&tx);
    CHECK_EQ_INT(n, 5 + 10 + 8);
    rx_init(&rx);
    for (i = 0; i < n; i++) {
        put(&rx, buf_peek(&tx) + i, 1);
        while ((rc = frame_parse(&rx, NULL, &f)) == 0) {
            check_stream_frame(&f, got);
            got++;
        }
        CHECK_EQ_INT(rc, FRAME_EAGAIN);
    }
    CHECK_EQ_INT(got, 3);
    CHECK_EQ_INT(buf_readable(&rx), 0);
    buf_free(&rx);
    buf_free(&tx);
}

static void test_many_frames_one_append(void)
{
    buf_t tx, rx;
    frame_t f;
    size_t n, got = 0;

    n = build_stream(&tx);
    rx_init(&rx);
    put(&rx, buf_peek(&tx), n);
    while (frame_parse(&rx, NULL, &f) == 0) {
        check_stream_frame(&f, got);
        got++;
    }
    CHECK_EQ_INT(got, 3);
    CHECK_EQ_INT(frame_parse(&rx, NULL, &f), FRAME_EAGAIN);
    buf_free(&rx);
    buf_free(&tx);
}

static void round_trip_one(size_t plen)
{
    buf_t tx, tx2, rx;
    frame_t f;
    const uint8_t *wire;
    size_t n;

    fill(big_payload, plen, 0x41);
    CHECK_EQ_INT(buf_init(&tx, RX_CAP), 0);
    CHECK_EQ_INT(frame_encode(&tx, 0x11, big_payload, plen), 0);
    wire = buf_peek(&tx);
    n = buf_readable(&tx);
    CHECK_EQ_INT(n, 4 + 1 + plen);
    rx_init(&rx);
    put(&rx, wire, n);
    CHECK_EQ_INT(frame_parse(&rx, NULL, &f), 0);
    CHECK_EQ_INT(f.type, 0x11);
    CHECK_EQ_INT(f.len, plen);
    CHECK_EQ_MEM(f.payload, big_payload, plen);
    CHECK_EQ_INT(buf_readable(&rx), 0);
    CHECK_EQ_INT(buf_init(&tx2, RX_CAP), 0);
    CHECK_EQ_INT(frame_encode(&tx2, f.type, f.payload, f.len), 0);
    CHECK_EQ_INT(buf_readable(&tx2), n);
    CHECK_EQ_MEM(buf_peek(&tx2), wire, n);
    buf_free(&rx);
    buf_free(&tx2);
    buf_free(&tx);
}

static void test_round_trip(void)
{
    round_trip_one(0);
    round_trip_one(1);
    round_trip_one(255);
    round_trip_one(4096);
    round_trip_one(FRAME_PAYLOAD_MAX);
}

static void test_encode_enospc_atomic(void)
{
    buf_t tx;

    CHECK_EQ_INT(buf_init(&tx, 8), 0);
    CHECK_EQ_INT(frame_encode(&tx, 0x11, "abcd", 4), BUF_ENOSPC);
    CHECK_EQ_INT(buf_readable(&tx), 0);
    CHECK_EQ_INT(frame_encode(&tx, 0x11, "abc", 3), 0);
    CHECK_EQ_INT(buf_readable(&tx), 8);
    buf_free(&tx);
}

static void test_null_args(void)
{
    buf_t tx;
    frame_t f;

    CHECK_EQ_INT(frame_parse(NULL, NULL, &f), -EINVAL);
    CHECK_EQ_INT(buf_init(&tx, RX_CAP), 0);
    CHECK_EQ_INT(frame_parse(&tx, NULL, NULL), -EINVAL);
    CHECK_EQ_INT(frame_parse(&tx, NULL, &f), FRAME_EAGAIN);
    buf_free(&tx);
}

int main(void)
{
    RUN(test_eagain_no_consume);
    RUN(test_len_zero);
    RUN(test_too_big_from_header_only);
    RUN(test_bad_type_at_fifth_byte);
    RUN(test_types_mask);
    RUN(test_exact_max_frame);
    RUN(test_one_over_max_refused);
    RUN(test_byte_by_byte_stream);
    RUN(test_many_frames_one_append);
    RUN(test_round_trip);
    RUN(test_encode_enospc_atomic);
    RUN(test_null_args);
    return test_summary();
}
