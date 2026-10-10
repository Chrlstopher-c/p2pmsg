/* peer/manager — encodage/décodage des messages pair à pair (HELLO, MSG, ACK, PING, PONG, BYE). */
#include <stdio.h>
#include <string.h>

#include "core/log/log.h"
#include "peer/manager/pm_internal.h"

#define PM_MOD "peer/manager"

int pm_wire_send(pm_link_t *l, const msg_t *m, uint64_t now)
{
    buf_t out;
    const uint8_t *data;
    size_t total;
    int rc;

    if (buf_init(&out, FRAME_MAX) != 0) {
        return -1;
    }
    rc = msg_encode(&out, m);
    if (rc == 0) {
        data = buf_peek(&out);
        total = buf_readable(&out);
        rc = sess_send(l->sess, data[4], data + FRAME_HDR, total - FRAME_HDR);
    }
    buf_free(&out);
    if (rc == 0) {
        l->last_tx_ms = now;
    }
    return rc;
}

static void wire_simple(pm_link_t *l, msg_type type, uint64_t now)
{
    msg_t m;

    memset(&m, 0, sizeof m);
    m.type = type;
    (void)pm_wire_send(l, &m, now);
}

static void wire_ack(pm_link_t *l, uint64_t id, uint64_t now)
{
    msg_t m;

    memset(&m, 0, sizeof m);
    m.type = MSG_ACK;
    m.u.ack.id = id;
    (void)pm_wire_send(l, &m, now);
}

static void wire_hello(pm_link_t *l, const pm *pm, uint64_t now)
{
    msg_t m;

    memset(&m, 0, sizeof m);
    m.type = MSG_HELLO;
    m.u.hello.version = MSG_PROTO_VERSION;
    snprintf(m.u.hello.name, sizeof m.u.hello.name, "%s", pm->name);
    (void)pm_wire_send(l, &m, now);
}

void pm_wire_hello(pm_link_t *l, const pm *pm, uint64_t now)
{
    wire_hello(l, pm, now);
}

static void wire_on_hello(pm_link_t *l, const msg_t *m, uint64_t now)
{
    pm_peer_t *p = l->peer;
    pm *pm = l->pm;

    if (l->hello_ok) {
        LOG_W(PM_MOD, "HELLO en double, fermeture");
        sess_close(l->sess, 0);
        return;
    }
    l->hello_ok = 1;
    snprintf(p->name, sizeof p->name, "%s", m->u.hello.name);
    pm_peer_backoff_reset(pm, p);
    pm_peer_set_state(pm, p);
    pm_queue_pump(pm, l, now);
}

static void wire_on_msg(pm_link_t *l, const msg_t *m, uint64_t now)
{
    pm_peer_t *p = l->peer;
    pm *pm = l->pm;

    wire_ack(l, m->u.msg.id, now);
    if (pm_peer_ring_seen(p, m->u.msg.id)) {
        LOG_D(PM_MOD, "doublon ignoré id=%016llx", (unsigned long long)m->u.msg.id);
        return;
    }
    pm_peer_ring_add(p, m->u.msg.id);
    if (pm->ev.on_message) {
        pm->ev.on_message(p->pub, m->u.msg.id, m->u.msg.ts_ms, m->u.msg.text, pm->ud);
    }
}

void pm_wire_on_frame(pm_link_t *l, uint8_t type, const uint8_t *pl, size_t len)
{
    frame_t f;
    msg_t m;
    uint64_t now;

    if (!l->pm || !l->peer) {
        return;
    }
    now = loop_now_ms(l->pm->loop);
    l->last_rx_ms = now;
    f.type = type;
    f.payload = pl;
    f.len = len;
    if (msg_decode(&f, &m) != 0) {
        LOG_W(PM_MOD, "trame malformée, fermeture");
        sess_close(l->sess, 0);
        return;
    }
    if (m.type == MSG_HELLO) {
        wire_on_hello(l, &m, now);
        return;
    }
    if (!l->hello_ok) {
        LOG_W(PM_MOD, "trame avant HELLO, fermeture");
        sess_close(l->sess, 0);
        return;
    }
    if (m.type == MSG_MSG) {
        wire_on_msg(l, &m, now);
    } else if (m.type == MSG_ACK) {
        pm_queue_ack(l->pm, l->peer, m.u.ack.id);
    } else if (m.type == MSG_PING) {
        wire_simple(l, MSG_PONG, now);
    } else if (m.type == MSG_BYE) {
        sess_close(l->sess, 1);
    }
}

void pm_wire_ping_check(pm *pm, pm_link_t *l, uint64_t now)
{
    if (now - l->last_rx_ms >= pm_scale(pm, PM_DEAD_MS)) {
        LOG_W(PM_MOD, "pair muet, fermeture");
        sess_close(l->sess, 0);
        return;
    }
    if (l->hello_ok && now - l->last_tx_ms >= pm_scale(pm, PM_PING_MS)) {
        wire_simple(l, MSG_PING, now);
    }
}
