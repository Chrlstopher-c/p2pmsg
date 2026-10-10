#define _GNU_SOURCE
/* Harnais partagé des tests sec/session : deux sessions en loopback dans une même loop, pair brut
 * (handshake via sess_noise, trames émises à la main) pour les cas d'erreur. Header seul, static inline. */
#ifndef P2P_TESTS_SEC_SESSION_HARNESS_H
#define P2P_TESTS_SEC_SESSION_HARNESS_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test.h"
#include "core/buf/buf.h"
#include "core/loop/loop.h"
#include "net/tcp/tcp.h"
#include "proto/frame/frame.h"
#include "sec/session/session.h"
#include "sec/session/sess_noise.h"

#define HARNESS_PROLOGUE     "p2pmsg/1"
#define HARNESS_MSG_MAX      4096u
#define HARNESS_PATH_MAX     512u

typedef struct side {
    sess_t        *sess;
    const sess_identity *id;
    const uint8_t *peer_expected;
    int            verify_rc;
    int            verify_calls;
    int            verify_key_ok;
    int            est;
    int            closes;
    int            close_err;
    int            msgs;
    uint8_t        last_type;
    size_t         last_len;
    uint8_t        last_buf[HARNESS_MSG_MAX];
} side_t;

typedef struct {
    loop_t        *loop;
    tcp_listener_t *ls;
    uint16_t       port;
    sess_identity  id_ini;
    sess_identity  id_resp;
    side_t         ini;
    side_t         resp;
    char           dir[HARNESS_PATH_MAX];
} world_t;

static inline int h_verify(sess_t *s, const uint8_t peer[SESS_KEY_LEN], void *ud)
{
    side_t *sd = ud;
    (void)s;
    sd->verify_calls++;
    if (!sd->peer_expected || memcmp(peer, sd->peer_expected, SESS_KEY_LEN) == 0) {
        sd->verify_key_ok = 1;
    }
    return sd->verify_rc;
}

static inline void h_est(sess_t *s, void *ud)
{
    side_t *sd = ud;
    (void)s;
    sd->est++;
}

static inline void h_msg(sess_t *s, uint8_t type, const uint8_t *p, size_t len, void *ud)
{
    side_t *sd = ud;
    (void)s;
    sd->msgs++;
    sd->last_type = type;
    sd->last_len = len;
    if (len > 0 && len <= sizeof(sd->last_buf)) {
        memcpy(sd->last_buf, p, len);
    }
}

static inline void h_close(sess_t *s, int err, void *ud)
{
    side_t *sd = ud;
    (void)s;
    sd->closes++;
    sd->close_err = err;
    sd->sess = NULL;
}

static const sess_handlers HARNESS_HANDLERS = { h_verify, h_est, h_msg, h_close };

static inline void accept_cb(tcp_listener_t *ls, tcp_conn_t *c, void *ud)
{
    world_t *w = ud;
    (void)ls;
    w->resp.sess = sess_new(w->loop, c, 1, SESS_RESPONDER, &w->id_resp, &HARNESS_HANDLERS, &w->resp);
}

static inline void harness_pump(world_t *w, uint64_t ms)
{
    uint64_t deadline = loop_now_ms(w->loop) + ms;

    while (loop_now_ms(w->loop) < deadline) {
        loop_run_once(w->loop, 1);
    }
}

static inline int harness_ids(world_t *w)
{
    char a[HARNESS_PATH_MAX + 32];
    char b[HARNESS_PATH_MAX + 32];

    snprintf(w->dir, sizeof(w->dir), "/tmp/p2psess-XXXXXX");
    if (!mkdtemp(w->dir)) {
        return -1;
    }
    snprintf(a, sizeof(a), "%s/ini.key", w->dir);
    snprintf(b, sizeof(b), "%s/resp.key", w->dir);
    if (sess_identity_load_or_create(a, &w->id_ini) != 0) {
        return -1;
    }
    return sess_identity_load_or_create(b, &w->id_resp);
}

static inline void harness_ids_free(world_t *w)
{
    char a[HARNESS_PATH_MAX + 32];

    if (w->dir[0] == '\0') {
        return;
    }
    snprintf(a, sizeof(a), "%s/ini.key", w->dir);
    unlink(a);
    snprintf(a, sizeof(a), "%s/resp.key", w->dir);
    unlink(a);
    rmdir(w->dir);
}

/* Écoute + initiateur connecté ; l'accept crée la session répondeur. */
static inline void harness_pair(world_t *w)
{
    static const tcp_handlers EMPTY = { NULL, NULL, NULL };
    tcp_conn_t *c;

    w->ls = tcp_listen(w->loop, "127.0.0.1", 0, accept_cb, w);
    CHECK(w->ls != NULL);
    w->port = w->ls ? tcp_listener_port(w->ls) : 0;
    w->ini.id = &w->id_ini;
    w->ini.peer_expected = w->id_resp.pub;
    w->resp.id = &w->id_resp;
    w->resp.peer_expected = w->id_ini.pub;
    c = tcp_connect(w->loop, "127.0.0.1", w->port, &EMPTY, NULL);
    CHECK(c != NULL);
    w->ini.sess = c ? sess_new(w->loop, c, 0, SESS_INITIATOR, &w->id_ini, &HARNESS_HANDLERS, &w->ini) : NULL;
}

static inline void harness_close_all(world_t *w)
{
    if (w->ini.sess) {
        sess_close(w->ini.sess, 0);
    }
    if (w->resp.sess) {
        sess_close(w->resp.sess, 0);
    }
    harness_pump(w, 100);
}

/* Pompe la boucle jusqu'à *v >= target ou plafond HARNESS_WAIT_MS (large, pour charge valgrind). */
#define HARNESS_WAIT_MS 5000u
static inline int harness_wait_ms(world_t *w, const int *v, int target, uint64_t ceiling_ms)
{
    uint64_t deadline = loop_now_ms(w->loop) + ceiling_ms;

    while (*v < target && loop_now_ms(w->loop) < deadline) {
        loop_run_once(w->loop, 5);
    }
    return *v >= target;
}

static inline int harness_wait(world_t *w, const int *v, int target)
{
    return harness_wait_ms(w, v, target, HARNESS_WAIT_MS);
}

static inline void harness_teardown(world_t *w)
{
    if (w->ls) {
        tcp_listener_close(w->ls);
    }
    loop_free(w->loop);
    harness_ids_free(w);
}

/* Pair brut : handshake via sess_noise (initiateur) et trames émises directement. */
typedef enum { RAW_HS, RAW_DATA_FIRST, RAW_SILENT } raw_mode;

typedef struct {
    loop_t        *loop;
    tcp_conn_t    *c;
    sn_t          *n;
    buf_t          rx;
    raw_mode       mode;
    int            established;
    int            closes;
    int            close_err;
    const sess_identity *id;
} raw_t;

static inline void raw_send_frame(raw_t *p, uint8_t type, const void *pl, size_t len)
{
    buf_t out;

    if (buf_init(&out, FRAME_MAX) != 0) {
        return;
    }
    if (frame_encode(&out, type, pl, len) == 0) {
        tcp_send(p->c, buf_peek(&out), buf_readable(&out));
    }
    buf_free(&out);
}

static inline void raw_send_hs1(raw_t *p)
{
    uint8_t out[256];
    size_t len = 0;

    if (sn_write_hs(p->n, NULL, 0, out, sizeof(out), &len) == 0) {
        raw_send_frame(p, SESS_FRAME_HS, out, len);
    }
}

static inline void raw_on_connect(tcp_conn_t *c, void *ud)
{
    raw_t *p = ud;
    static const uint8_t JUNK[1] = { 0x00 };

    (void)c;
    if (p->mode == RAW_HS) {
        raw_send_hs1(p);
    } else if (p->mode == RAW_DATA_FIRST) {
        raw_send_frame(p, SESS_FRAME_DATA, JUNK, 1);
    }
}

static inline int raw_handle_hs(raw_t *p, const uint8_t *pl, size_t len)
{
    uint8_t tmp[256];
    size_t plen = 0;
    size_t out_len = 0;

    if (p->established || sn_read_hs(p->n, pl, len, tmp, sizeof(tmp), &plen) != 0) {
        return -1;
    }
    if (sn_write_hs(p->n, NULL, 0, tmp, sizeof(tmp), &out_len) != 0) {
        return -1;
    }
    raw_send_frame(p, SESS_FRAME_HS, tmp, out_len);
    if (sn_split(p->n) != 0) {
        return -1;
    }
    p->established = 1;
    return 0;
}

static inline void raw_on_data(tcp_conn_t *c, buf_t *rx, void *ud)
{
    raw_t *p = ud;
    frame_types allowed;
    frame_t f;

    (void)c;
    memset(&allowed, 0, sizeof(allowed));
    frame_types_allow(&allowed, SESS_FRAME_HS);
    frame_types_allow(&allowed, SESS_FRAME_DATA);
    while (frame_parse(rx, &allowed, &f) == 0) {
        if (f.type == SESS_FRAME_HS && !p->established) {
            raw_handle_hs(p, f.payload, f.len);
        }
    }
}

static inline void raw_on_close(tcp_conn_t *c, int err, void *ud)
{
    raw_t *p = ud;
    (void)c;
    p->closes++;
    p->close_err = err;
    p->c = NULL;
}

static inline void raw_connect(world_t *w, raw_t *p, raw_mode mode)
{
    tcp_handlers h;
    sn_opts o;

    memset(p, 0, sizeof(*p));
    p->loop = w->loop;
    p->mode = mode;
    p->id = &w->id_ini;
    o.static_priv = w->id_ini.priv;
    o.eph_priv = NULL;
    o.prologue = HARNESS_PROLOGUE;
    o.prologue_len = strlen(HARNESS_PROLOGUE);
    p->n = sn_new(SESS_INITIATOR, &o);
    CHECK(p->n != NULL);
    CHECK(buf_init(&p->rx, FRAME_MAX * 2) == 0);
    h.on_connect = raw_on_connect;
    h.on_data = raw_on_data;
    h.on_close = raw_on_close;
    p->c = tcp_connect(w->loop, "127.0.0.1", w->port, &h, p);
    CHECK(p->c != NULL);
}

static inline void raw_free(raw_t *p)
{
    sn_free(p->n);
    buf_free(&p->rx);
    p->n = NULL;
}

/* Trame DATA chiffrée avec la clé transport du pair brut (après handshake). */
static inline int raw_send_encrypted(raw_t *p, uint8_t type, const uint8_t *pl, size_t len, int corrupt)
{
    uint8_t plain[HARNESS_MSG_MAX];
    uint8_t ct[HARNESS_MSG_MAX];

    plain[0] = type;
    if (len > 0) {
        memcpy(plain + 1, pl, len);
    }
    if (sn_encrypt(p->n, plain, len + 1, ct) != 0) {
        return -1;
    }
    if (corrupt) {
        ct[0] ^= 0x01;
    }
    raw_send_frame(p, SESS_FRAME_DATA, ct, len + 1 + SN_MAC_LEN);
    return 0;
}

#endif
