/* core/buf — buffer d'octets croissant borné + lecture/écriture big-endian bornées. Pas thread-safe. */
#ifndef P2P_CORE_BUF_H
#define P2P_CORE_BUF_H
#include <stddef.h>
#include <stdint.h>

#define BUF_ENOSPC (-1001) /* dépasserait cap_max */
#define BUF_ESHORT (-1002) /* lecture au-delà des données disponibles */

typedef struct {
    uint8_t *data;
    size_t   len;     /* octets valides dans data[rpos..len) non consommés à partir de rpos */
    size_t   rpos;    /* curseur de lecture */
    size_t   cap;
    size_t   cap_max; /* plafond dur d'allocation */
} buf_t;

int  buf_init(buf_t *b, size_t cap_max);            /* 0 ou -ENOMEM ; n'alloue rien avant 1re écriture */
void buf_free(buf_t *b);                            /* idempotent */
void buf_reset(buf_t *b);                           /* len = rpos = 0, garde l'allocation */
size_t buf_readable(const buf_t *b);                /* len - rpos */
const uint8_t *buf_peek(const buf_t *b);            /* data + rpos (NULL si vide) */
void buf_consume(buf_t *b, size_t n);               /* avance rpos de min(n, readable) */
void buf_compact(buf_t *b);                         /* déplace la partie non lue au début */

int buf_append(buf_t *b, const void *src, size_t n); /* 0, BUF_ENOSPC ou -ENOMEM ; compacte si utile */
int buf_put_u8(buf_t *b, uint8_t v);
int buf_put_u32be(buf_t *b, uint32_t v);
int buf_put_u64be(buf_t *b, uint64_t v);

/* Lectures consommantes : 0 ou BUF_ESHORT (rien consommé dans ce cas). */
int buf_get_u8(buf_t *b, uint8_t *out);
int buf_get_u32be(buf_t *b, uint32_t *out);
int buf_get_u64be(buf_t *b, uint64_t *out);
int buf_get_bytes(buf_t *b, void *dst, size_t n);

/* Lecture d'un entier BE dans un tableau brut, sans buf_t. p doit contenir 4/8 octets. */
uint32_t buf_load_u32be(const uint8_t *p);
uint64_t buf_load_u64be(const uint8_t *p);
void     buf_store_u32be(uint8_t *p, uint32_t v);
void     buf_store_u64be(uint8_t *p, uint64_t v);

#endif
