/* proto/msg : messages applicatifs en payload de trames. Encodage BE exact (cf. msg.h), UTF-8 strict.
 * Interprétations strictes : HELLO name et MSG text non vides (len >= 1), BYE reason peut être vide.
 * Pur : aucune allocation, aucune I/O. En cas d'erreur de msg_decode, *out est indéterminé. */
#include "proto/msg/msg.h"

#include <errno.h>
#include <string.h>

#define MSG_HDR_LEN_HELLO 2u   /* version + name_len */
#define MSG_HDR_LEN_MSG   18u  /* id + ts_ms + text_len */
#define MSG_HDR_LEN_BYE   2u   /* reason_len */
#define MSG_ACK_LEN       8u
#define MSG_PAYLOAD_MAX   (MSG_HDR_LEN_MSG + MSG_TEXT_MAX)

void msg_allowed_types(frame_types *t)
{
    static const uint8_t ALLOWED[] = {MSG_HELLO, MSG_MSG, MSG_ACK, MSG_PING, MSG_PONG, MSG_BYE};
    size_t i;

    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof *t);
    for (i = 0; i < sizeof ALLOWED / sizeof ALLOWED[0]; i++) {
        frame_types_allow(t, ALLOWED[i]);
    }
}

static int in_range(uint8_t v, uint8_t lo, uint8_t hi)
{
    return v >= lo && v <= hi;
}

/* Longueur du caractère UTF-8 commençant par s[0] (n >= 1 octets dispo), 0 si invalide. */
static size_t utf8_char_len(const uint8_t *s, size_t n)
{
    uint8_t c = s[0];
    uint8_t lo = 0x80;
    uint8_t hi = 0xBF;
    size_t len;

    if (c <= 0x7F) {
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        len = 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
        len = 3;
        lo = (c == 0xE0) ? 0xA0 : lo;
        hi = (c == 0xED) ? 0x9F : hi;
    } else if (c >= 0xF0 && c <= 0xF4) {
        len = 4;
        lo = (c == 0xF0) ? 0x90 : lo;
        hi = (c == 0xF4) ? 0x8F : hi;
    } else {
        return 0;
    }
    if (n < len || !in_range(s[1], lo, hi)) {
        return 0;
    }
    for (size_t i = 2; i < len; i++) {
        if (!in_range(s[i], 0x80, 0xBF)) {
            return 0;
        }
    }
    return len;
}

int msg_utf8_valid(const uint8_t *s, size_t n)
{
    size_t i = 0;
    size_t l;

    if (n == 0) {
        return 1;
    }
    if (s == NULL) {
        return 0;
    }
    while (i < n) {
        l = utf8_char_len(s + i, n - i);
        if (l == 0) {
            return 0;
        }
        i += l;
    }
    return 1;
}

/* 0, MSG_EMALFORMED (NUL interne) ou MSG_EUTF8. */
static int text_check(const uint8_t *s, size_t n)
{
    if (n > 0 && memchr(s, 0, n) != NULL) {
        return MSG_EMALFORMED;
    }
    if (!msg_utf8_valid(s, n)) {
        return MSG_EUTF8;
    }
    return 0;
}

/* Longueur de s terminée par NUL, ou -1 si pas de NUL dans s[0..max]. */
static int bounded_strlen(const char *s, size_t max)
{
    size_t i;

    for (i = 0; i <= max; i++) {
        if (s[i] == '\0') {
            return (int)i;
        }
    }
    return -1;
}

static int payload_hello(const msg_t *m, uint8_t *p, size_t *len)
{
    int n = bounded_strlen(m->u.hello.name, MSG_NAME_MAX);
    int rc;

    if (n < 1) {
        return MSG_EMALFORMED;
    }
    rc = text_check((const uint8_t *)m->u.hello.name, (size_t)n);
    if (rc != 0) {
        return rc;
    }
    p[0] = m->u.hello.version;
    p[1] = (uint8_t)n;
    memcpy(p + MSG_HDR_LEN_HELLO, m->u.hello.name, (size_t)n);
    *len = MSG_HDR_LEN_HELLO + (size_t)n;
    return 0;
}

static int payload_msg(const msg_t *m, uint8_t *p, size_t *len)
{
    size_t n = m->u.msg.len;
    int rc;

    if (n < 1 || n > MSG_TEXT_MAX) {
        return MSG_EMALFORMED;
    }
    rc = text_check((const uint8_t *)m->u.msg.text, n);
    if (rc != 0) {
        return rc;
    }
    buf_store_u64be(p, m->u.msg.id);
    buf_store_u64be(p + 8, m->u.msg.ts_ms);
    p[16] = (uint8_t)(n >> 8);
    p[17] = (uint8_t)(n & 0xFFu);
    memcpy(p + MSG_HDR_LEN_MSG, m->u.msg.text, n);
    *len = MSG_HDR_LEN_MSG + n;
    return 0;
}

static int payload_bye(const msg_t *m, uint8_t *p, size_t *len)
{
    size_t n = m->u.bye.len;
    int rc;

    if (n > MSG_REASON_MAX) {
        return MSG_EMALFORMED;
    }
    rc = text_check((const uint8_t *)m->u.bye.reason, n);
    if (rc != 0) {
        return rc;
    }
    p[0] = (uint8_t)(n >> 8);
    p[1] = (uint8_t)(n & 0xFFu);
    memcpy(p + MSG_HDR_LEN_BYE, m->u.bye.reason, n);
    *len = MSG_HDR_LEN_BYE + n;
    return 0;
}

static int build_payload(const msg_t *m, uint8_t *p, size_t *len)
{
    switch (m->type) {
    case MSG_HELLO:
        return payload_hello(m, p, len);
    case MSG_MSG:
        return payload_msg(m, p, len);
    case MSG_ACK:
        buf_store_u64be(p, m->u.ack.id);
        *len = MSG_ACK_LEN;
        return 0;
    case MSG_PING:
    case MSG_PONG:
        *len = 0;
        return 0;
    case MSG_BYE:
        return payload_bye(m, p, len);
    default:
        return MSG_EMALFORMED;
    }
}

int msg_encode(buf_t *out, const msg_t *m)
{
    uint8_t p[MSG_PAYLOAD_MAX];
    size_t len = 0;
    int rc;

    if (out == NULL || m == NULL) {
        return -EINVAL;
    }
    rc = build_payload(m, p, &len);
    if (rc != 0) {
        return rc;
    }
    return frame_encode(out, (uint8_t)m->type, p, len);
}

/* Chaque décodeur valide la longueur exacte avant de lire, puis le texte, puis remplit out. */
static int decode_hello(const uint8_t *p, size_t n, msg_t *out)
{
    size_t name_len;
    int rc;

    if (n < MSG_HDR_LEN_HELLO) {
        return MSG_EMALFORMED;
    }
    name_len = p[1];
    if (name_len < 1 || name_len > MSG_NAME_MAX || n != MSG_HDR_LEN_HELLO + name_len) {
        return MSG_EMALFORMED;
    }
    rc = text_check(p + MSG_HDR_LEN_HELLO, name_len);
    if (rc != 0) {
        return rc;
    }
    out->u.hello.version = p[0];
    memcpy(out->u.hello.name, p + MSG_HDR_LEN_HELLO, name_len);
    out->u.hello.name[name_len] = '\0';
    return 0;
}

static int decode_msg(const uint8_t *p, size_t n, msg_t *out)
{
    size_t text_len;
    int rc;

    if (n < MSG_HDR_LEN_MSG) {
        return MSG_EMALFORMED;
    }
    text_len = ((size_t)p[16] << 8) | p[17];
    if (text_len < 1 || text_len > MSG_TEXT_MAX || n != MSG_HDR_LEN_MSG + text_len) {
        return MSG_EMALFORMED;
    }
    rc = text_check(p + MSG_HDR_LEN_MSG, text_len);
    if (rc != 0) {
        return rc;
    }
    out->u.msg.id = buf_load_u64be(p);
    out->u.msg.ts_ms = buf_load_u64be(p + 8);
    out->u.msg.len = (uint16_t)text_len;
    memcpy(out->u.msg.text, p + MSG_HDR_LEN_MSG, text_len);
    out->u.msg.text[text_len] = '\0';
    return 0;
}

static int decode_ack(const uint8_t *p, size_t n, msg_t *out)
{
    if (n != MSG_ACK_LEN) {
        return MSG_EMALFORMED;
    }
    out->u.ack.id = buf_load_u64be(p);
    return 0;
}

static int decode_bye(const uint8_t *p, size_t n, msg_t *out)
{
    size_t reason_len;
    int rc;

    if (n < MSG_HDR_LEN_BYE) {
        return MSG_EMALFORMED;
    }
    reason_len = ((size_t)p[0] << 8) | p[1];
    if (reason_len > MSG_REASON_MAX || n != MSG_HDR_LEN_BYE + reason_len) {
        return MSG_EMALFORMED;
    }
    rc = text_check(p + MSG_HDR_LEN_BYE, reason_len);
    if (rc != 0) {
        return rc;
    }
    out->u.bye.len = (uint16_t)reason_len;
    memcpy(out->u.bye.reason, p + MSG_HDR_LEN_BYE, reason_len);
    out->u.bye.reason[reason_len] = '\0';
    return 0;
}

static int decode_dispatch(uint8_t type, const uint8_t *p, size_t n, msg_t *out)
{
    switch (type) {
    case MSG_HELLO:
        return decode_hello(p, n, out);
    case MSG_MSG:
        return decode_msg(p, n, out);
    case MSG_ACK:
        return decode_ack(p, n, out);
    case MSG_PING:
    case MSG_PONG:
        return n == 0 ? 0 : MSG_EMALFORMED;
    case MSG_BYE:
        return decode_bye(p, n, out);
    default:
        return MSG_EMALFORMED;
    }
}

int msg_decode(const frame_t *f, msg_t *out)
{
    int rc;

    if (f == NULL || out == NULL) {
        return -EINVAL;
    }
    if (f->len > 0 && f->payload == NULL) {
        return MSG_EMALFORMED;
    }
    rc = decode_dispatch(f->type, f->payload, f->len, out);
    if (rc == 0) {
        out->type = (msg_type)f->type;
    }
    return rc;
}
