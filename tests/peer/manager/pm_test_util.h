/* Utilitaires de test de peer/manager : identités, enregistreur d'évènements, proxy TCP, pair factice. */
#ifndef P2P_TEST_PM_UTIL_H
#define P2P_TEST_PM_UTIL_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>

#include "core/buf/buf.h"
#include "core/loop/loop.h"
#include "net/tcp/tcp.h"
#include "peer/manager/manager.h"
#include "proto/frame/frame.h"
#include "proto/msg/msg.h"
#include "sec/session/session.h"

#define UTIL_DIV 100u

typedef struct {
    int delivered;
    int failed;
    int msgs;
    int connected_events;
    uint64_t ids[64];
    int n_ids;
    char name[MSG_NAME_MAX + 1];
    char last_text[MSG_TEXT_MAX + 1];
    pm_state st;
} util_rec_t;

static inline void util_rec_on_peer(const uint8_t pub[32], const char *name, pm_state st, void *ud)
{
    util_rec_t *r = ud;

    (void)pub;
    r->st = st;
    if (st == PM_CONNECTED) {
        r->connected_events++;
        snprintf(r->name, sizeof r->name, "%s", name);
    }
}

static inline void util_rec_on_message(const uint8_t pub[32], uint64_t id, uint64_t ts, const char *text, void *ud)
{
    util_rec_t *r = ud;

    (void)pub;
    (void)ts;
    r->msgs++;
    if (r->n_ids < 64) {
        r->ids[r->n_ids++] = id;
    }
    snprintf(r->last_text, sizeof r->last_text, "%s", text);
}

static inline void util_rec_on_delivered(const uint8_t pub[32], uint64_t id, void *ud)
{
    util_rec_t *r = ud;

    (void)pub;
    (void)id;
    r->delivered++;
}

static inline void util_rec_on_failed(const uint8_t pub[32], uint64_t id, void *ud)
{
    util_rec_t *r = ud;

    (void)pub;
    (void)id;
    r->failed++;
}

static inline pm_events util_events(void)
{
    pm_events e = {
        util_rec_on_peer, util_rec_on_message, util_rec_on_delivered, util_rec_on_failed
    };
    return e;
}

static inline int util_identity(sess_identity *id, const char *dir, const char *name)
{
    char path[PATH_MAX];

    snprintf(path, sizeof path, "%s/%s.key", dir, name);
    return sess_identity_load_or_create(path, id);
}

static inline void util_tmpdir(char *out, size_t n)
{
    snprintf(out, n, "/tmp/pm-test-%ld", (long)getpid());
    if (mkdir(out, 0700) != 0 && errno != EEXIST) {
        out[0] = '\0';
    }
}

static inline void util_cfg(pm_config *cfg, const sess_identity *id, const char *name)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->identity = id;
    cfg->name = name;
    cfg->listen_host = "127.0.0.1";
    cfg->listen_port = 0;
    cfg->time_div = UTIL_DIV;
    cfg->disc.enable_mdns = 0;
    cfg->disc.peers_file = NULL;
}

typedef int (*util_cond_fn)(void *ud);

/* Fait tourner la boucle jusqu'à cond(ud) != 0 ou timeout (ms réels). 1 si la condition est atteinte. */
static inline int util_wait(loop_t *l, util_cond_fn cond, void *ud, uint64_t timeout_ms)
{
    uint64_t end = loop_now_ms(l) + timeout_ms;

    while (!cond(ud)) {
        if (loop_now_ms(l) >= end) {
            return 0;
        }
        (void)loop_run_once(l, 1);
    }
    return 1;
}

/* Envoie un message applicatif (encodé via msg_encode) par sess_send. */
static inline int util_sess_msg(sess_t *s, const msg_t *m)
{
    buf_t out;
    const uint8_t *d;
    int rc;

    if (buf_init(&out, FRAME_MAX) != 0) {
        return -1;
    }
    rc = msg_encode(&out, m);
    if (rc == 0) {
        d = buf_peek(&out);
        rc = sess_send(s, d[4], d + FRAME_HDR, buf_readable(&out) - FRAME_HDR);
    }
    buf_free(&out);
    return rc;
}

/* ---- Proxy TCP : relais, coupure, suppression de N trames de données venant de B ---- */

typedef struct util_pair util_pair_t;

typedef struct {
    loop_t *loop;
    tcp_listener_t *ls;
    uint16_t target;
    int cut;
    int drop_b;        /* nombre de trames transport (0x02) B->A à supprimer */
    int seen_b_data;   /* trames transport B->A vues (supprimées ou non) */
    int live;          /* paires vivantes */
    util_pair_t *pairs;
} util_proxy_t;

struct util_pair {
    util_proxy_t *px;
    tcp_conn_t *a;
    tcp_conn_t *b;
    util_pair_t *next;
};

static inline void util_pair_unlink(util_proxy_t *px, util_pair_t *pr)
{
    util_pair_t **pp = &px->pairs;

    while (*pp && *pp != pr) {
        pp = &(*pp)->next;
    }
    if (*pp) {
        *pp = pr->next;
    }
}

static inline void util_pair_close_side(util_pair_t *pr, tcp_conn_t *c)
{
    if (c == pr->a) {
        pr->a = NULL;
    } else {
        pr->b = NULL;
    }
    if (pr->a) {
        tcp_close(pr->a, 0);
    }
    if (pr->b) {
        tcp_close(pr->b, 0);
    }
    if (!pr->a && !pr->b) {
        pr->px->live--;
        util_pair_unlink(pr->px, pr);
        free(pr);
    }
}

static inline void util_px_on_close(tcp_conn_t *c, int err, void *ud)
{
    (void)err;
    util_pair_close_side(ud, c);
}

static inline void util_px_forward(util_pair_t *pr, tcp_conn_t *from, buf_t *rx)
{
    tcp_conn_t *to = (from == pr->a) ? pr->b : pr->a;
    const uint8_t *p;
    size_t n;
    uint32_t L;
    int drop;

    while (buf_readable(rx) >= 4) {
        p = buf_peek(rx);
        L = buf_load_u32be(p);
        if (buf_readable(rx) < 4u + L) {
            return;
        }
        drop = 0;
        if (from == pr->b && L >= 1 && p[4] == 0x02) {
            pr->px->seen_b_data++;
            if (pr->px->drop_b > 0) {
                pr->px->drop_b--;
                drop = 1;
            }
        }
        n = 4u + L;
        if (!drop && to) {
            (void)tcp_send(to, p, n);
        }
        buf_consume(rx, n);
    }
}

static inline void util_px_on_data(tcp_conn_t *c, buf_t *rx, void *ud)
{
    util_pair_t *pr = ud;

    if (!pr->a || !pr->b) {
        buf_reset(rx);
        return;
    }
    util_px_forward(pr, c, rx);
}

static const tcp_handlers UTIL_PX_HANDLERS = { NULL, util_px_on_data, util_px_on_close };

static inline void util_px_accept(tcp_listener_t *ls, tcp_conn_t *c, void *ud)
{
    util_proxy_t *px = ud;
    util_pair_t *pr;

    (void)ls;
    if (px->cut) {
        tcp_close(c, 0);
        return;
    }
    pr = calloc(1, sizeof *pr);
    if (!pr) {
        tcp_close(c, 0);
        return;
    }
    pr->px = px;
    pr->a = c;
    pr->next = px->pairs;
    px->pairs = pr;
    px->live++;
    tcp_set_handlers(c, &UTIL_PX_HANDLERS, pr);
    pr->b = tcp_connect(px->loop, "127.0.0.1", px->target, &UTIL_PX_HANDLERS, pr);
    if (!pr->b) {
        util_pair_close_side(pr, c);
    }
}

static inline int util_px_start(util_proxy_t *px, loop_t *l, uint16_t target)
{
    memset(px, 0, sizeof *px);
    px->loop = l;
    px->target = target;
    px->ls = tcp_listen(l, "127.0.0.1", 0, util_px_accept, px);
    return px->ls ? 0 : -1;
}

static inline uint16_t util_px_port(const util_proxy_t *px)
{
    return tcp_listener_port(px->ls);
}

/* Coupe : ferme toutes les paires et refuse les nouvelles connexions. */
static inline void util_px_cut(util_proxy_t *px, int on)
{
    util_pair_t *pr;
    util_pair_t *next;

    px->cut = on;
    if (!on) {
        return;
    }
    for (pr = px->pairs; pr; pr = next) {
        next = pr->next;
        if (pr->a) {
            tcp_close(pr->a, 0);
        }
        if (pr->b) {
            tcp_close(pr->b, 0);
        }
    }
}

/* ---- Pair factice : handshake réel, HELLO, PONG sur PING, comptage des MSG, pas d'ACK ---- */

typedef struct {
    loop_t *loop;
    sess_identity id;
    tcp_listener_t *ls;
    int accepts;
    int closes;
    int msgs;
    int pings;
    int hellos;
    int reply_pong;
    int close_at_accept;
    char name[MSG_NAME_MAX + 1];
    sess_t *last;
} util_fake_t;

static inline void util_fake_on_message(sess_t *s, uint8_t type, const uint8_t *pl, size_t len, void *ud)
{
    util_fake_t *f = ud;
    frame_t fr = { type, pl, len };
    msg_t m;
    msg_t out;

    if (msg_decode(&fr, &m) != 0) {
        return;
    }
    if (m.type == MSG_HELLO) {
        f->hellos++;
    } else if (m.type == MSG_MSG) {
        f->msgs++;
    } else if (m.type == MSG_PING) {
        f->pings++;
        if (f->reply_pong) {
            memset(&out, 0, sizeof out);
            out.type = MSG_PONG;
            (void)util_sess_msg(s, &out);
        }
    }
}

static inline void util_fake_on_established(sess_t *s, void *ud)
{
    util_fake_t *f = ud;
    msg_t h;

    memset(&h, 0, sizeof h);
    h.type = MSG_HELLO;
    h.u.hello.version = MSG_PROTO_VERSION;
    snprintf(h.u.hello.name, sizeof h.u.hello.name, "%s", f->name);
    (void)util_sess_msg(s, &h);
}

static inline int util_fake_verify(sess_t *s, const uint8_t pub[32], void *ud)
{
    (void)s;
    (void)pub;
    (void)ud;
    return 0;
}

static inline void util_fake_on_close(sess_t *s, int err, void *ud)
{
    util_fake_t *f = ud;

    (void)s;
    (void)err;
    f->closes++;
}

static inline void util_fake_accept(tcp_listener_t *ls, tcp_conn_t *c, void *ud)
{
    util_fake_t *f = ud;
    sess_handlers h;

    (void)ls;
    f->accepts++;
    if (f->close_at_accept) {
        tcp_close(c, 0);
        return;
    }
    memset(&h, 0, sizeof h);
    h.on_verify = util_fake_verify;
    h.on_established = util_fake_on_established;
    h.on_message = util_fake_on_message;
    h.on_close = util_fake_on_close;
    f->last = sess_new(f->loop, c, 1, SESS_RESPONDER, &f->id, &h, f);
}

static inline int util_fake_start(util_fake_t *f, loop_t *l, const char *dir, const char *name)
{
    memset(f, 0, sizeof *f);
    f->loop = l;
    f->reply_pong = 1;
    snprintf(f->name, sizeof f->name, "%s", name);
    if (util_identity(&f->id, dir, name) != 0) {
        return -1;
    }
    f->ls = tcp_listen(l, "127.0.0.1", 0, util_fake_accept, f);
    return f->ls ? 0 : -1;
}

/* Fait tourner la boucle pendant ms (fermetures asynchrones, libérations). */
static inline void util_drain(loop_t *l, uint64_t ms)
{
    uint64_t end = loop_now_ms(l) + ms;

    while (loop_now_ms(l) < end) {
        (void)loop_run_once(l, 5);
    }
}

static inline void util_px_stop(util_proxy_t *px)
{
    util_px_cut(px, 1);
    if (px->ls) {
        tcp_listener_close(px->ls);
        px->ls = NULL;
    }
}

static inline void util_fake_stop(util_fake_t *f)
{
    if (f->ls) {
        tcp_listener_close(f->ls);
        f->ls = NULL;
    }
}

#endif
