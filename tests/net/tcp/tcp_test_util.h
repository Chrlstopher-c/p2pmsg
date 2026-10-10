/* Helpers partagés par les tests net/tcp : boucle pilotée, compteurs de callbacks. */
#ifndef P2P_TCP_TEST_UTIL_H
#define P2P_TCP_TEST_UTIL_H
#include <stdint.h>
#include "core/loop/loop.h"
#include "net/tcp/tcp.h"
#include "../../test.h"

#define TU_TIMEOUT_MS 5000u

/* Itère la boucle jusqu'à *done ou timeout. Retourne 1 si *done est vrai. */
static inline int tu_run_until(loop_t *l, const volatile int *done)
{
    uint64_t start = loop_now_ms(l);
    while (!*done && loop_now_ms(l) - start < TU_TIMEOUT_MS) {
        loop_run_once(l, 10);
    }
    return *done ? 1 : 0;
}

/* Donne du temps à la boucle (pour laisser les timers 0 ms et les I/O passer). */
static inline void tu_pump(loop_t *l, int rounds)
{
    for (int i = 0; i < rounds; i++) {
        loop_run_once(l, 1);
    }
}

typedef struct {
    int connects;
    int closes;
    int last_err;
    int data_calls;
    size_t rx_total;
    volatile int closed;
} tu_probe_t;

static inline void tu_on_connect(tcp_conn_t *c, void *ud)
{
    (void)c;
    ((tu_probe_t *)ud)->connects++;
}

static inline void tu_on_data(tcp_conn_t *c, buf_t *rx, void *ud)
{
    (void)c;
    tu_probe_t *p = (tu_probe_t *)ud;
    p->data_calls++;
    p->rx_total += buf_readable(rx);
    buf_consume(rx, buf_readable(rx));
}

static inline void tu_on_close(tcp_conn_t *c, int err, void *ud)
{
    (void)c;
    tu_probe_t *p = (tu_probe_t *)ud;
    p->closes++;
    p->last_err = err;
    p->closed = 1;
}

static inline tcp_handlers tu_handlers(void)
{
    tcp_handlers h = { tu_on_connect, tu_on_data, tu_on_close };
    return h;
}

#endif
