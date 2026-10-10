#define _GNU_SOURCE
/* sec/session : session Noise XX sur tcp_conn. Handshake, vérification, transport chiffré, fermeture unique. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "core/log/log.h"
#include "sec/session/sess_noise.h"
#include "sec/session/session.h"

#define SESS_MOD        "sec/session"
#define SESS_PROLOGUE   "p2pmsg/1"
#define HS_BUF_MAX      256u
#define HS_PAYLOAD_MAX  16u
#define FLUSH_RETRY_MS  1u

static const uint8_t NO_PAYLOAD[1] = {0};

struct sess {
    loop_t        *loop;
    tcp_conn_t    *c;
    sess_role      role;
    sess_handlers  h;
    void          *ud;
    sn_t          *n;
    buf_t          txq;        /* trames déjà chiffrées, en attente d'espace TCP (ordre = ordre des nonces) */
    frame_types    hs_types;
    frame_types    data_types;
    uint8_t        peer[SESS_KEY_LEN];
    int            has_peer;
    int            established;
    int            hs_step;
    int            closing;
    int            err;
    uint64_t       hs_timer;
    uint64_t       flush_timer;
};

static void timers_cancel(sess_t *s)
{
    loop_timer_cancel(s->loop, s->hs_timer);
    loop_timer_cancel(s->loop, s->flush_timer);
    s->hs_timer = 0;
    s->flush_timer = 0;
}

static void flush_queue(sess_t *s);

static void sess_shutdown(sess_t *s, int err, int graceful)
{
    if (s->closing) {
        return;
    }
    s->closing = 1;
    if (s->err == 0) {
        s->err = err;
    }
    if (s->err != 0) {
        LOG_W(SESS_MOD, "session closed with error %d", s->err);
    }
    if (graceful) {
        flush_queue(s);
    }
    timers_cancel(s);
    tcp_close(s->c, graceful);
}

static void hs_timeout_cb(loop_t *l, uint64_t id, void *ud)
{
    sess_t *s = ud;
    (void)l;
    (void)id;
    s->hs_timer = 0;
    sess_shutdown(s, SESS_ETIMEOUT, 0);
}

static void flush_cb(loop_t *l, uint64_t id, void *ud)
{
    sess_t *s = ud;
    (void)l;
    (void)id;
    s->flush_timer = 0;
    flush_queue(s);
}

static void flush_queue(sess_t *s)
{
    while (buf_readable(&s->txq) >= FRAME_HDR) {
        const uint8_t *p = buf_peek(&s->txq);
        size_t total = (size_t)buf_load_u32be(p) + 4u;
        if (total > buf_readable(&s->txq)) {
            sess_shutdown(s, SESS_EPROTO, 0);
            return;
        }
        if (tcp_send(s->c, p, total) != 0) {
            if (s->flush_timer == 0 && !s->closing) {
                s->flush_timer = loop_timer_add(s->loop, FLUSH_RETRY_MS, 0, flush_cb, s);
            }
            return;
        }
        buf_consume(&s->txq, total);
    }
}

static int queue_frame(sess_t *s, uint8_t type, const void *p, size_t len)
{
    if (buf_readable(&s->txq) + FRAME_HDR + len > TCP_TX_MAX) {
        return TCP_ETXFULL;
    }
    int rc = frame_encode(&s->txq, type, p, len);
    if (rc == 0) {
        flush_queue(s);
    }
    return rc;
}

static void hs_send(sess_t *s)
{
    uint8_t out[HS_BUF_MAX];
    size_t out_len = 0;
    int rc = sn_write_hs(s->n, NO_PAYLOAD, 0, out, sizeof(out), &out_len);
    if (rc == 0) {
        rc = queue_frame(s, SESS_FRAME_HS, out, out_len);
    }
    if (rc != 0) {
        sess_shutdown(s, rc, 0);
    }
}

static int hs_verify(sess_t *s)
{
    if (sn_remote_static(s->n, s->peer) != 0) {
        return SESS_EPROTO;
    }
    s->has_peer = 1;
    if (s->h.on_verify(s, s->peer, s->ud) != 0) {
        LOG_W(SESS_MOD, "peer key rejected by on_verify");
        return SESS_EREJECTED;
    }
    return 0;
}

static void hs_established(sess_t *s)
{
    if (sn_split(s->n) != 0) {
        sess_shutdown(s, SESS_EPROTO, 0);
        return;
    }
    s->established = 1;
    loop_timer_cancel(s->loop, s->hs_timer);
    s->hs_timer = 0;
    if (s->h.on_established) {
        s->h.on_established(s, s->ud);
    }
}

/* Initiateur : reçoit msg2, vérifie, envoie msg3. Répondeur : reçoit msg1 → msg2, puis msg3 → vérifie. */
static void hs_on_frame(sess_t *s, const frame_t *f)
{
    uint8_t pl[HS_PAYLOAD_MAX];
    size_t plen = 0;
    int rc = sn_read_hs(s->n, f->payload, f->len, pl, sizeof(pl), &plen);
    if (rc != 0) {
        sess_shutdown(s, rc, 0);
        return;
    }
    if (s->role == SESS_RESPONDER && s->hs_step == 0) {
        s->hs_step = 1;
        hs_send(s);
        return;
    }
    rc = hs_verify(s);
    if (rc != 0) {
        sess_shutdown(s, rc, 0);
        return;
    }
    if (s->role == SESS_INITIATOR) {
        hs_send(s);
    }
    if (!s->closing) {
        hs_established(s);
    }
}

static void data_on_frame(sess_t *s, const frame_t *f)
{
    size_t ptlen = f->len >= SN_MAC_LEN ? f->len - SN_MAC_LEN : 0;
    if (ptlen < 1) {
        sess_shutdown(s, SESS_EPROTO, 0);
        return;
    }
    uint8_t *pt = malloc(ptlen);
    if (!pt) {
        sess_shutdown(s, -ENOMEM, 0);
        return;
    }
    int rc = sn_decrypt(s->n, f->payload, f->len, pt);
    if (rc != 0) {
        explicit_bzero(pt, ptlen);
        free(pt);
        sess_shutdown(s, rc, 0);
        return;
    }
    if (!s->closing) {
        s->h.on_message(s, pt[0], pt + 1, ptlen - 1, s->ud);
    }
    explicit_bzero(pt, ptlen);
    free(pt);
}

static void on_connect(tcp_conn_t *c, void *ud)
{
    sess_t *s = ud;
    (void)c;
    if (s->role == SESS_INITIATOR) {
        hs_send(s);
    }
}

static void on_data(tcp_conn_t *c, buf_t *rx, void *ud)
{
    sess_t *s = ud;
    (void)c;
    while (!s->closing) {
        frame_t f;
        int rc = frame_parse(rx, s->established ? &s->data_types : &s->hs_types, &f);
        if (rc == FRAME_EAGAIN) {
            return;
        }
        if (rc != 0) {
            sess_shutdown(s, SESS_EPROTO, 0);
            return;
        }
        if (f.type == SESS_FRAME_HS) {
            hs_on_frame(s, &f);
        } else {
            data_on_frame(s, &f);
        }
    }
}

static void on_close(tcp_conn_t *c, int err, void *ud)
{
    sess_t *s = ud;
    (void)c;
    int e = s->err != 0 ? s->err : err;
    timers_cancel(s);
    buf_free(&s->txq);
    sn_free(s->n);
    s->h.on_close(s, e, s->ud);
    free(s);
}

static void sess_release(sess_t *s)
{
    if (s->hs_timer) {
        loop_timer_cancel(s->loop, s->hs_timer);
    }
    buf_free(&s->txq);
    sn_free(s->n);
    free(s);
}

static sess_t *sess_alloc(loop_t *l, tcp_conn_t *c, sess_role role, const sess_identity *id,
                          const sess_handlers *h, void *ud)
{
    sess_t *s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    sn_opts o = {
        .static_priv = id->priv,
        .eph_priv = NULL,
        .prologue = SESS_PROLOGUE,
        .prologue_len = sizeof(SESS_PROLOGUE) - 1u,
    };
    s->n = sn_new(role, &o);
    if (!s->n || buf_init(&s->txq, TCP_TX_MAX) != 0) {
        sess_release(s);
        return NULL;
    }
    s->loop = l;
    s->c = c;
    s->role = role;
    s->h = *h;
    s->ud = ud;
    frame_types_allow(&s->hs_types, SESS_FRAME_HS);
    frame_types_allow(&s->data_types, SESS_FRAME_DATA);
    s->hs_timer = loop_timer_add(l, SESS_HS_TIMEOUT_MS, 0, hs_timeout_cb, s);
    if (s->hs_timer == 0) {
        sess_release(s);
        return NULL;
    }
    return s;
}

sess_t *sess_new(loop_t *l, tcp_conn_t *c, int connected, sess_role role, const sess_identity *id,
                 const sess_handlers *h, void *ud)
{
    if (!l || !c || !id || !h || !h->on_verify || !h->on_close) {
        if (c) {
            tcp_close(c, 0);
        }
        return NULL;
    }
    sess_t *s = sess_alloc(l, c, role, id, h, ud);
    if (!s) {
        LOG_E(SESS_MOD, "session allocation failed");
        tcp_close(c, 0);
        return NULL;
    }
    tcp_handlers th = { .on_connect = on_connect, .on_data = on_data, .on_close = on_close };
    tcp_set_handlers(c, &th, s);
    if (connected && role == SESS_INITIATOR) {
        hs_send(s);
    }
    return s;
}

int sess_send(sess_t *s, uint8_t type, const void *payload, size_t len)
{
    if (!s || !s->established || s->closing) {
        return -ENOTCONN;
    }
    if (len > SESS_PLAINTEXT_MAX) {
        return -EMSGSIZE;
    }
    size_t ctlen = len + 1u + SN_MAC_LEN;
    if (buf_readable(&s->txq) + FRAME_HDR + ctlen > TCP_TX_MAX) {
        return TCP_ETXFULL;
    }
    uint8_t *pt = malloc(len + 1u);
    uint8_t *ct = malloc(ctlen);
    if (!pt || !ct) {
        free(pt);
        free(ct);
        return -ENOMEM;
    }
    pt[0] = type;
    if (len > 0) {
        memcpy(pt + 1, payload, len);
    }
    int rc = sn_encrypt(s->n, pt, len + 1u, ct);
    explicit_bzero(pt, len + 1u);
    free(pt);
    if (rc == 0) {
        rc = queue_frame(s, SESS_FRAME_DATA, ct, ctlen);
    }
    free(ct);
    if (rc != 0) {
        sess_shutdown(s, rc, 0);
    }
    return rc;
}

void sess_close(sess_t *s, int graceful)
{
    if (s && !s->closing) {
        sess_shutdown(s, 0, graceful);
    }
}

int sess_is_established(const sess_t *s)
{
    return s && s->established;
}

const uint8_t *sess_peer_key(const sess_t *s)
{
    return s && s->has_peer ? s->peer : NULL;
}

tcp_conn_t *sess_conn(sess_t *s)
{
    return s ? s->c : NULL;
}
