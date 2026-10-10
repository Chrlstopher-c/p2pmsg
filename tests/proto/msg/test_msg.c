/* Tests de proto/msg : round-trip par type, vecteurs d'octets écrits à la main, types autorisés. */
#include <stdint.h>
#include <string.h>

#include "core/buf/buf.h"
#include "proto/frame/frame.h"
#include "proto/msg/msg.h"
#include "test.h"

static int encode_to(buf_t *b, const msg_t *m)
{
    CHECK_EQ_INT(buf_init(b, FRAME_MAX), 0);
    return msg_encode(b, m);
}

static void roundtrip(const msg_t *in)
{
    buf_t orig;
    buf_t b;
    buf_t again;
    frame_types allowed;
    frame_t f;
    msg_t out;
    size_t n;

    CHECK_EQ_INT(encode_to(&orig, in), 0);
    CHECK_EQ_INT(encode_to(&b, in), 0);
    n = buf_readable(&b);
    msg_allowed_types(&allowed);
    CHECK_EQ_INT(frame_parse(&b, &allowed, &f), 0);
    CHECK_EQ_INT(buf_readable(&b), 0);
    CHECK_EQ_INT(msg_decode(&f, &out), 0);
    CHECK_EQ_INT(out.type, in->type);
    CHECK_EQ_INT(encode_to(&again, &out), 0);
    CHECK_EQ_INT(buf_readable(&again), n);
    CHECK_EQ_MEM(buf_peek(&again), buf_peek(&orig), n);
    buf_free(&again);
    buf_free(&b);
    buf_free(&orig);
}

static void test_roundtrip_hello(void)
{
    msg_t m;
    msg_t out;
    buf_t b;
    frame_types allowed;
    frame_t f;
    char name64[MSG_NAME_MAX + 1];

    memset(&m, 0, sizeof m);
    memset(name64, 'n', MSG_NAME_MAX);
    name64[MSG_NAME_MAX] = '\0';
    m.type = MSG_HELLO;
    m.u.hello.version = MSG_PROTO_VERSION;
    memcpy(m.u.hello.name, name64, sizeof name64);
    CHECK_EQ_INT(encode_to(&b, &m), 0);
    msg_allowed_types(&allowed);
    CHECK_EQ_INT(frame_parse(&b, &allowed, &f), 0);
    CHECK_EQ_INT(msg_decode(&f, &out), 0);
    CHECK_EQ_INT(out.u.hello.version, MSG_PROTO_VERSION);
    CHECK_EQ_INT(strlen(out.u.hello.name), MSG_NAME_MAX);
    CHECK(strcmp(out.u.hello.name, name64) == 0);
    buf_free(&b);
    roundtrip(&m);
}

static void test_roundtrip_msg(void)
{
    msg_t m;
    msg_t out;
    buf_t b;
    frame_types allowed;
    frame_t f;

    memset(&m, 0, sizeof m);
    m.type = MSG_MSG;
    m.u.msg.id = 0x1122334455667788ULL;
    m.u.msg.ts_ms = 1700000000123ULL;
    m.u.msg.len = MSG_TEXT_MAX;
    memset(m.u.msg.text, 'x', MSG_TEXT_MAX);
    CHECK_EQ_INT(encode_to(&b, &m), 0);
    msg_allowed_types(&allowed);
    CHECK_EQ_INT(frame_parse(&b, &allowed, &f), 0);
    CHECK_EQ_INT(msg_decode(&f, &out), 0);
    CHECK(out.u.msg.id == m.u.msg.id);
    CHECK(out.u.msg.ts_ms == m.u.msg.ts_ms);
    CHECK_EQ_INT(out.u.msg.len, MSG_TEXT_MAX);
    CHECK_EQ_INT(out.u.msg.text[MSG_TEXT_MAX], 0);
    CHECK_EQ_MEM(out.u.msg.text, m.u.msg.text, MSG_TEXT_MAX);
    buf_free(&b);
}

static void test_roundtrip_ack_ping_pong_bye(void)
{
    msg_t m;

    memset(&m, 0, sizeof m);
    m.type = MSG_ACK;
    m.u.ack.id = 42;
    roundtrip(&m);
    m.type = MSG_PING;
    roundtrip(&m);
    m.type = MSG_PONG;
    roundtrip(&m);
    m.type = MSG_BYE;
    m.u.bye.len = 0;
    roundtrip(&m);
    m.u.bye.len = MSG_REASON_MAX;
    memset(m.u.bye.reason, 'r', MSG_REASON_MAX);
    roundtrip(&m);
}

static void test_vector_msg(void)
{
    static const uint8_t EXPECT[] = {
        0x00, 0x00, 0x00, 0x15, 0x11,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x68, 0x69,
    };
    msg_t m;
    buf_t b;

    memset(&m, 0, sizeof m);
    m.type = MSG_MSG;
    m.u.msg.id = 1;
    m.u.msg.ts_ms = 2;
    m.u.msg.len = 2;
    memcpy(m.u.msg.text, "hi", 2);
    CHECK_EQ_INT(encode_to(&b, &m), 0);
    CHECK_EQ_INT(buf_readable(&b), sizeof EXPECT);
    CHECK_EQ_MEM(buf_peek(&b), EXPECT, sizeof EXPECT);
    buf_free(&b);
}

static void test_vector_hello(void)
{
    static const uint8_t EXPECT[] = {0x00, 0x00, 0x00, 0x06, 0x10, 0x01, 0x03, 0x41, 0x6E, 0x61};
    msg_t m;
    buf_t b;

    memset(&m, 0, sizeof m);
    m.type = MSG_HELLO;
    m.u.hello.version = 1;
    memcpy(m.u.hello.name, "Ana", 4);
    CHECK_EQ_INT(encode_to(&b, &m), 0);
    CHECK_EQ_INT(buf_readable(&b), sizeof EXPECT);
    CHECK_EQ_MEM(buf_peek(&b), EXPECT, sizeof EXPECT);
    buf_free(&b);
}

static void test_vector_ack_ping_bye(void)
{
    static const uint8_t ACK[] = {0x00, 0x00, 0x00, 0x09, 0x12, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    static const uint8_t PING[] = {0x00, 0x00, 0x00, 0x01, 0x13};
    static const uint8_t BYE[] = {0x00, 0x00, 0x00, 0x03, 0x15, 0x00, 0x00};
    msg_t m;
    buf_t b;

    memset(&m, 0, sizeof m);
    m.type = MSG_ACK;
    m.u.ack.id = 0x0102030405060708ULL;
    CHECK_EQ_INT(encode_to(&b, &m), 0);
    CHECK_EQ_MEM(buf_peek(&b), ACK, sizeof ACK);
    buf_free(&b);
    m.type = MSG_PING;
    CHECK_EQ_INT(encode_to(&b, &m), 0);
    CHECK_EQ_MEM(buf_peek(&b), PING, sizeof PING);
    buf_free(&b);
    memset(&m, 0, sizeof m);
    m.type = MSG_BYE;
    CHECK_EQ_INT(encode_to(&b, &m), 0);
    CHECK_EQ_MEM(buf_peek(&b), BYE, sizeof BYE);
    buf_free(&b);
}

static void test_allowed_types(void)
{
    frame_types t;
    int i;

    msg_allowed_types(&t);
    for (i = 0; i < 256; i++) {
        int bit = (t.bits[i / 8] >> (i % 8)) & 1;
        int want = (i >= 0x10 && i <= 0x15);
        CHECK_EQ_INT(bit, want);
    }
}

static void test_decode_hello_other_version(void)
{
    static const uint8_t P[] = {0x02, 0x01, 0x41};
    frame_t f = {.type = MSG_HELLO, .payload = P, .len = sizeof P};
    msg_t out;

    CHECK_EQ_INT(msg_decode(&f, &out), 0);
    CHECK_EQ_INT(out.u.hello.version, 2);
    CHECK(strcmp(out.u.hello.name, "A") == 0);
}

int main(void)
{
    RUN(test_roundtrip_hello);
    RUN(test_roundtrip_msg);
    RUN(test_roundtrip_ack_ping_pong_bye);
    RUN(test_vector_msg);
    RUN(test_vector_hello);
    RUN(test_vector_ack_ping_bye);
    RUN(test_allowed_types);
    RUN(test_decode_hello_other_version);
    return test_summary();
}
