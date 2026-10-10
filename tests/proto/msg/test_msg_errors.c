/* Tests d'erreurs de proto/msg : bornes, longueurs exactes, NUL, UTF-8 à l'encodage et au décodage. */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "core/buf/buf.h"
#include "proto/msg/msg.h"
#include "test.h"

#define BIG_TEXT_LEN (MSG_TEXT_MAX + 1)
#define BIG_REASON_LEN (MSG_REASON_MAX + 1)

static int dec(uint8_t type, const uint8_t *p, size_t n)
{
    frame_t f = {.type = type, .payload = p, .len = n};
    msg_t out;

    return msg_decode(&f, &out);
}

static int enc_msg(uint16_t len, const char *text, size_t tlen)
{
    msg_t m;
    buf_t b;
    int rc;

    memset(&m, 0, sizeof m);
    m.type = MSG_MSG;
    m.u.msg.len = len;
    memcpy(m.u.msg.text, text, tlen);
    CHECK_EQ_INT(buf_init(&b, FRAME_MAX), 0);
    rc = msg_encode(&b, &m);
    if (rc != 0) {
        CHECK_EQ_INT(buf_readable(&b), 0);
    }
    buf_free(&b);
    return rc;
}

static void test_hello_decode_errors(void)
{
    static const uint8_t EMPTY[] = {0};
    static const uint8_t NAME0[] = {0x01, 0x00};
    static const uint8_t SHORT[] = {0x01, 0x03, 0x41};
    static const uint8_t EXTRA[] = {0x01, 0x01, 0x41, 0x42};
    static const uint8_t NUL[] = {0x01, 0x02, 0x41, 0x00};
    static const uint8_t BADUTF8[] = {0x01, 0x01, 0xC0};

    CHECK_EQ_INT(dec(MSG_HELLO, EMPTY, 0), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_HELLO, EMPTY, 1), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_HELLO, NAME0, sizeof NAME0), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_HELLO, SHORT, sizeof SHORT), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_HELLO, EXTRA, sizeof EXTRA), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_HELLO, NUL, sizeof NUL), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_HELLO, BADUTF8, sizeof BADUTF8), MSG_EUTF8);
}

static void test_hello_name_max(void)
{
    uint8_t p[2 + MSG_NAME_MAX + 1];

    memset(p, 'n', sizeof p);
    p[0] = 1;
    p[1] = MSG_NAME_MAX;
    CHECK_EQ_INT(dec(MSG_HELLO, p, 2 + MSG_NAME_MAX), 0);
    p[1] = MSG_NAME_MAX + 1;
    CHECK_EQ_INT(dec(MSG_HELLO, p, sizeof p), MSG_EMALFORMED);
}

static void test_hello_encode_errors(void)
{
    msg_t m;
    buf_t b;
    char long_name[MSG_NAME_MAX + 2];

    memset(&m, 0, sizeof m);
    m.type = MSG_HELLO;
    CHECK_EQ_INT(buf_init(&b, FRAME_MAX), 0);
    CHECK_EQ_INT(msg_encode(&b, &m), MSG_EMALFORMED);
    memcpy(m.u.hello.name, "\xC3\x28", 3);
    CHECK_EQ_INT(msg_encode(&b, &m), MSG_EUTF8);
    memset(long_name, 'a', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = '\0';
    memcpy(m.u.hello.name, long_name, sizeof m.u.hello.name);
    CHECK_EQ_INT(msg_encode(&b, &m), MSG_EMALFORMED);
    CHECK_EQ_INT(buf_readable(&b), 0);
    buf_free(&b);
}

static void test_msg_decode_errors(void)
{
    static const uint8_t TOO_SHORT[17] = {0};
    static const uint8_t NO_TEXT[18] = {0};
    static const uint8_t BAD_UTF8[19] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0xFF};
    static const uint8_t NUL_IN[21] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 'a', 0, 'b'};
    static const uint8_t NUL_FIRST[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0xC0};
    uint8_t body[18 + 2];

    CHECK_EQ_INT(dec(MSG_MSG, TOO_SHORT, sizeof TOO_SHORT), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_MSG, NO_TEXT, sizeof NO_TEXT), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_MSG, BAD_UTF8, sizeof BAD_UTF8), MSG_EUTF8);
    CHECK_EQ_INT(dec(MSG_MSG, NUL_IN, sizeof NUL_IN), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_MSG, NUL_FIRST, sizeof NUL_FIRST), MSG_EMALFORMED);
    memset(body, 0, sizeof body);
    body[17] = 2;
    CHECK_EQ_INT(dec(MSG_MSG, body, sizeof body - 1), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_MSG, body, sizeof body + 1), MSG_EMALFORMED);
}

static void test_msg_text_bounds(void)
{
    static uint8_t body[18 + BIG_TEXT_LEN];

    memset(body, 'x', sizeof body);
    body[16] = (uint8_t)(BIG_TEXT_LEN >> 8);
    body[17] = (uint8_t)(BIG_TEXT_LEN & 0xFF);
    CHECK_EQ_INT(dec(MSG_MSG, body, sizeof body), MSG_EMALFORMED);
    body[16] = (uint8_t)(MSG_TEXT_MAX >> 8);
    body[17] = (uint8_t)(MSG_TEXT_MAX & 0xFF);
    CHECK_EQ_INT(dec(MSG_MSG, body, 18 + MSG_TEXT_MAX), 0);
    CHECK_EQ_INT(enc_msg((uint16_t)BIG_TEXT_LEN, "x", 1), MSG_EMALFORMED);
    CHECK_EQ_INT(enc_msg(0, "x", 1), MSG_EMALFORMED);
    CHECK_EQ_INT(enc_msg(3, "\xED\xA0\x80", 3), MSG_EUTF8);
    CHECK_EQ_INT(enc_msg(2, "a\0", 2), MSG_EMALFORMED);
    CHECK_EQ_INT(enc_msg(1, "a", 1), 0);
}

static void test_ack_ping_pong_errors(void)
{
    static const uint8_t SEVEN[7] = {0};
    static const uint8_t NINE[9] = {0};
    static const uint8_t ONE[1] = {0};

    CHECK_EQ_INT(dec(MSG_ACK, SEVEN, sizeof SEVEN), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_ACK, NINE, sizeof NINE), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_ACK, NULL, 0), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_PING, ONE, sizeof ONE), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_PONG, ONE, sizeof ONE), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_PONG, NULL, 0), 0);
}

static void test_bye_errors(void)
{
    static const uint8_t ONE[1] = {0};
    static const uint8_t LEN_NO_DATA[2] = {0x00, 0x01};
    static const uint8_t TRAILING[3] = {0x00, 0x00, 0x00};
    static const uint8_t BAD_UTF8[3] = {0x00, 0x01, 0xF5};
    static uint8_t big[2 + BIG_REASON_LEN];

    CHECK_EQ_INT(dec(MSG_BYE, ONE, sizeof ONE), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_BYE, LEN_NO_DATA, sizeof LEN_NO_DATA), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_BYE, TRAILING, sizeof TRAILING), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(MSG_BYE, BAD_UTF8, sizeof BAD_UTF8), MSG_EUTF8);
    memset(big, 'r', sizeof big);
    big[0] = (uint8_t)(BIG_REASON_LEN >> 8);
    big[1] = (uint8_t)(BIG_REASON_LEN & 0xFF);
    CHECK_EQ_INT(dec(MSG_BYE, big, sizeof big), MSG_EMALFORMED);
}

static void test_unknown_type_and_args(void)
{
    msg_t m;
    buf_t b;

    CHECK_EQ_INT(dec(0x00, NULL, 0), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(0x16, NULL, 0), MSG_EMALFORMED);
    CHECK_EQ_INT(dec(0xFF, NULL, 0), MSG_EMALFORMED);
    CHECK_EQ_INT(msg_decode(NULL, &m), -EINVAL);
    CHECK_EQ_INT(msg_decode(&(frame_t){.type = MSG_PING}, NULL), -EINVAL);
    CHECK_EQ_INT(msg_decode(&(frame_t){.type = MSG_HELLO, .payload = NULL, .len = 3}, &m), MSG_EMALFORMED);
    CHECK_EQ_INT(msg_encode(NULL, &m), -EINVAL);
    CHECK_EQ_INT(buf_init(&b, FRAME_MAX), 0);
    CHECK_EQ_INT(msg_encode(&b, NULL), -EINVAL);
    buf_free(&b);
}

static void test_bye_encode_reason(void)
{
    msg_t m;
    buf_t b;
    char big[MSG_REASON_MAX + 1];

    memset(&m, 0, sizeof m);
    m.type = MSG_BYE;
    CHECK_EQ_INT(buf_init(&b, FRAME_MAX), 0);
    CHECK_EQ_INT(msg_encode(&b, &m), 0);
    memset(big, 'z', sizeof big);
    memcpy(m.u.bye.reason, big, sizeof big);
    m.u.bye.len = MSG_REASON_MAX + 1;
    CHECK_EQ_INT(msg_encode(&b, &m), MSG_EMALFORMED);
    memcpy(m.u.bye.reason, "a\xC0", 2);
    m.u.bye.len = 2;
    CHECK_EQ_INT(msg_encode(&b, &m), MSG_EUTF8);
    buf_free(&b);
}

int main(void)
{
    RUN(test_hello_decode_errors);
    RUN(test_hello_name_max);
    RUN(test_hello_encode_errors);
    RUN(test_msg_decode_errors);
    RUN(test_msg_text_bounds);
    RUN(test_ack_ping_pong_errors);
    RUN(test_bye_errors);
    RUN(test_unknown_type_and_args);
    RUN(test_bye_encode_reason);
    return test_summary();
}
