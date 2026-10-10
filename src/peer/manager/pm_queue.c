/* peer/manager — file d'envoi par pair : mise en file, ACK, retransmission (10 s, 5 envois max). */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "core/log/log.h"
#include "peer/manager/pm_internal.h"

#define PM_MOD "peer/manager"

static void entry_free(pm_entry_t *e)
{
    if (e) {
        free(e->text);
        free(e);
    }
}

int pm_queue_push(pm *pm, pm_peer_t *p, uint64_t id, uint64_t ts_ms, const char *text)
{
    pm_entry_t *e;
    size_t n;

    if (pm->q_total >= PM_QUEUE_MAX) {
        return -ENOSPC;
    }
    e = calloc(1, sizeof *e);
    if (!e) {
        return -ENOMEM;
    }
    n = strlen(text);
    e->text = malloc(n + 1u);
    if (!e->text) {
        free(e);
        return -ENOMEM;
    }
    memcpy(e->text, text, n + 1u);
    e->id = id;
    e->ts_ms = ts_ms;
    e->text_len = (uint16_t)n;
    if (p->q_tail) {
        p->q_tail->next = e;
    } else {
        p->q_head = e;
    }
    p->q_tail = e;
    p->q_len++;
    pm->q_total++;
    return 0;
}

/* Retire e de la file de p (e doit y être) et libère-le. */
static void queue_remove(pm *pm, pm_peer_t *p, pm_entry_t *prev, pm_entry_t *e)
{
    if (prev) {
        prev->next = e->next;
    } else {
        p->q_head = e->next;
    }
    if (p->q_tail == e) {
        p->q_tail = prev;
    }
    p->q_len--;
    pm->q_total--;
    entry_free(e);
}

void pm_queue_ack(pm *pm, pm_peer_t *p, uint64_t id)
{
    pm_entry_t *prev = NULL;
    pm_entry_t *e = p->q_head;

    while (e && e->id != id) {
        prev = e;
        e = e->next;
    }
    if (!e) {
        LOG_D(PM_MOD, "ack sans message en attente");
        return;
    }
    queue_remove(pm, p, prev, e);
    if (pm->ev.on_delivered) {
        pm->ev.on_delivered(p->pub, id, pm->ud);
    }
}

static int entry_transmit(pm_link_t *l, pm_entry_t *e, uint64_t now)
{
    msg_t m;

    memset(&m, 0, sizeof m);
    m.type = MSG_MSG;
    m.u.msg.id = e->id;
    m.u.msg.ts_ms = e->ts_ms;
    m.u.msg.len = e->text_len;
    memcpy(m.u.msg.text, e->text, e->text_len);
    if (pm_wire_send(l, &m, now) != 0) {
        return -1;
    }
    e->sends++;
    e->last_sent_ms = now;
    return 0;
}

/* Décide pour une entrée : envoi initial, renvoi, ou échec. Retourne 1 si l'entrée a été retirée. */
static int entry_step(pm *pm, pm_link_t *l, pm_peer_t *p, pm_entry_t *prev, pm_entry_t *e, uint64_t now)
{
    if (e->sends == 0) {
        (void)entry_transmit(l, e, now);
        return 0;
    }
    if (now - e->last_sent_ms < pm_scale(pm, PM_RETRY_MS)) {
        return 0;
    }
    if (e->sends >= PM_MAX_SENDS) {
        LOG_W(PM_MOD, "message abandonné après envois max id=%016llx", (unsigned long long)e->id);
        if (pm->ev.on_failed) {
            pm->ev.on_failed(p->pub, e->id, pm->ud);
        }
        queue_remove(pm, p, prev, e);
        return 1;
    }
    (void)entry_transmit(l, e, now);
    return 0;
}

void pm_queue_pump(pm *pm, pm_link_t *l, uint64_t now)
{
    pm_peer_t *p = l->peer;
    pm_entry_t *prev = NULL;
    pm_entry_t *e;
    pm_entry_t *next;

    if (!p || p->cur != l || !l->hello_ok) {
        return;
    }
    e = p->q_head;
    while (e) {
        next = e->next;
        if (entry_step(pm, l, p, prev, e, now) == 0) {
            prev = e;
        }
        e = next;
    }
}

void pm_queue_drop_all(pm *pm, pm_peer_t *p)
{
    pm_entry_t *e = p->q_head;
    pm_entry_t *next;

    while (e) {
        next = e->next;
        entry_free(e);
        e = next;
    }
    pm->q_total -= p->q_len;
    p->q_head = NULL;
    p->q_tail = NULL;
    p->q_len = 0;
}
