/* proto/frame — trames [len u32 BE][type u8][payload], len = 1 + taille payload. Pur, sans I/O. */
#ifndef P2P_PROTO_FRAME_H
#define P2P_PROTO_FRAME_H
#include <stddef.h>
#include <stdint.h>
#include "core/buf/buf.h"

#define FRAME_MAX       (64u * 1024u)   /* taille max de la trame entière, en-tête inclus */
#define FRAME_HDR       5u
#define FRAME_PAYLOAD_MAX (FRAME_MAX - FRAME_HDR)

#define FRAME_EAGAIN   1      /* pas assez de données, réessayer après lecture */
#define FRAME_ETOOBIG (-1201)
#define FRAME_EBADLEN (-1202) /* len == 0 */
#define FRAME_EBADTYPE (-1203)

/* Types valides : bitmask 256 bits fourni par l'appelant (proto/msg ou sec/session). */
typedef struct { uint8_t bits[32]; } frame_types;
void frame_types_allow(frame_types *t, uint8_t type);

typedef struct {
    uint8_t        type;
    const uint8_t *payload; /* pointe dans rx, valide jusqu'au prochain consume/append */
    size_t         len;
} frame_t;

/* Essaie d'extraire UNE trame de rx. 0 = trame dans out et consommée de rx ; FRAME_EAGAIN ;
 * erreur négative = flux invalide (l'appelant ferme la connexion). Avec allowed NULL, tout type accepté. */
int frame_parse(buf_t *rx, const frame_types *allowed, frame_t *out);

/* Écrit en-tête + payload dans out. 0, FRAME_ETOOBIG ou erreur buf_*. */
int frame_encode(buf_t *out, uint8_t type, const void *payload, size_t len);

#endif
