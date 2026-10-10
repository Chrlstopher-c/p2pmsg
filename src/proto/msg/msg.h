/* proto/msg — messages applicatifs (payloads de trames en clair, avant chiffrement par sec/session). */
#ifndef P2P_PROTO_MSG_H
#define P2P_PROTO_MSG_H
#include <stddef.h>
#include <stdint.h>
#include "core/buf/buf.h"
#include "proto/frame/frame.h"

#define MSG_PROTO_VERSION 1u
#define MSG_NAME_MAX      64u    /* octets UTF-8 */
#define MSG_TEXT_MAX      4096u  /* octets UTF-8 */
#define MSG_REASON_MAX    256u

typedef enum {
    MSG_HELLO = 0x10, MSG_MSG = 0x11, MSG_ACK = 0x12, MSG_PING = 0x13, MSG_PONG = 0x14, MSG_BYE = 0x15
} msg_type;

#define MSG_EMALFORMED (-1301)
#define MSG_EUTF8      (-1302)

/* Encodages des payloads (BE) :
 * HELLO : u8 version | u8 name_len | name
 * MSG   : u64 id | u64 ts_ms (epoch) | u16 text_len | text  (text_len <= MSG_TEXT_MAX, UTF-8 valide)
 * ACK   : u64 id
 * PING/PONG : vide
 * BYE   : u16 reason_len | reason (0 autorisé)
 * Tout octet en trop ou manquant = MSG_EMALFORMED. */
typedef struct {
    msg_type type;
    union {
        struct { uint8_t version; char name[MSG_NAME_MAX + 1]; } hello;
        struct { uint64_t id; uint64_t ts_ms; uint16_t len; char text[MSG_TEXT_MAX + 1]; } msg;
        struct { uint64_t id; } ack;
        struct { uint16_t len; char reason[MSG_REASON_MAX + 1]; } bye;
    } u;
} msg_t; /* chaînes toujours terminées par '\0' après décodage */

void msg_allowed_types(frame_types *t);
int  msg_encode(buf_t *out, const msg_t *m);        /* trame complète ; valide UTF-8 et bornes */
int  msg_decode(const frame_t *f, msg_t *out);      /* 0, MSG_EMALFORMED, MSG_EUTF8 */
int  msg_utf8_valid(const uint8_t *s, size_t n);    /* 1 si UTF-8 strict (pas de surrogates/overlong) */

#endif
