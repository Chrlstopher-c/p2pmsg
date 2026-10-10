/* Tests net/tcp : listen/connect/échange, intégrité 1 Mo, fermeture gracieuse, refus de connexion. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "tcp_test_util.h"

static tcp_listener_t *g_ls;
static tcp_conn_t *g_accepted;
static volatile int g_accept_seen;

static void on_accept(tcp_listener_t *ls, tcp_conn_t *c, void *ud)
{
    (void)ls;
    (void)ud;
    g_accepted = c;
    g_accept_seen = 1;
}

static void test_listen_port_and_connect(void)
{
    loop_t *l = loop_new();
    CHECK(l != NULL);
    g_accept_seen = 0;
    g_ls = tcp_listen(l, "127.0.0.1", 0, on_accept, NULL);
    CHECK(g_ls != NULL);
    uint16_t port = tcp_listener_port(g_ls);
    CHECK(port != 0);

    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *c = tcp_connect(l, "127.0.0.1", port, &h, &cp);
    CHECK(c != NULL);
    CHECK(tu_run_until(l, &g_accept_seen));
    CHECK(cp.connects == 1);
    tcp_listener_close(g_ls);
    tcp_close(c, 0);
    tcp_close(g_accepted, 0);
    tu_pump(l, 5);
    loop_free(l);
}

static void test_echo_exchange(void)
{
    loop_t *l = loop_new();
    g_accept_seen = 0;
    g_ls = tcp_listen(l, "127.0.0.1", 0, on_accept, NULL);
    uint16_t port = tcp_listener_port(g_ls);
    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *c = tcp_connect(l, "127.0.0.1", port, &h, &cp);
    CHECK(tu_run_until(l, &g_accept_seen));
    tu_probe_t sp = {0};
    tcp_set_handlers(g_accepted, &h, &sp);
    CHECK(tcp_send(c, "hello", 5) == 0);
    tu_pump(l, 20);
    CHECK(sp.rx_total == 5);
    tcp_listener_close(g_ls);
    tcp_close(c, 0);
    tcp_close(g_accepted, 0);
    tu_pump(l, 10);
    loop_free(l);
}

static void test_one_mib_integrity(void)
{
    enum { N = 1024 * 1024 };
    uint8_t *src = malloc(N);
    CHECK(src != NULL);
    for (size_t i = 0; src && i < N; i++) {
        src[i] = (uint8_t)(i * 31u + 7u);
    }
    loop_t *l = loop_new();
    g_accept_seen = 0;
    g_ls = tcp_listen(l, "127.0.0.1", 0, on_accept, NULL);
    uint16_t port = tcp_listener_port(g_ls);
    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *c = tcp_connect(l, "127.0.0.1", port, &h, &cp);
    CHECK(tu_run_until(l, &g_accept_seen));
    tu_probe_t sp = {0};
    tcp_set_handlers(g_accepted, &h, &sp);
    size_t sent = 0;
    while (sent < N) {
        size_t chunk = N - sent < 4096 ? N - sent : 4096;
        if (tcp_send(c, src + sent, chunk) == 0) {
            sent += chunk;
        }
        loop_run_once(l, 0);
    }
    for (int i = 0; i < 2000 && sp.rx_total < N; i++) {
        loop_run_once(l, 5);
    }
    CHECK(sp.rx_total == N);
    tcp_listener_close(g_ls);
    tcp_close(c, 0);
    tcp_close(g_accepted, 0);
    tu_pump(l, 10);
    loop_free(l);
    free(src);
}

static void test_connection_refused(void)
{
    loop_t *l = loop_new();
    /* Port libre : listener éphémère fermé immédiatement. */
    tcp_listener_t *tmp = tcp_listen(l, "127.0.0.1", 0, on_accept, NULL);
    uint16_t port = tcp_listener_port(tmp);
    tcp_listener_close(tmp);
    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *c = tcp_connect(l, "127.0.0.1", port, &h, &cp);
    CHECK(c != NULL);
    CHECK(tu_run_until(l, &cp.closed));
    CHECK(cp.closes == 1);
    CHECK_EQ_INT(cp.last_err, -ECONNREFUSED);
    CHECK(cp.connects == 0);
    tu_pump(l, 5);
    CHECK(cp.closes == 1);
    loop_free(l);
}

static void test_graceful_close_flushes_tx(void)
{
    enum { N = 300 * 1024 };
    static uint8_t src[N];
    memset(src, 0xAB, N);
    loop_t *l = loop_new();
    g_accept_seen = 0;
    g_ls = tcp_listen(l, "127.0.0.1", 0, on_accept, NULL);
    uint16_t port = tcp_listener_port(g_ls);
    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *c = tcp_connect(l, "127.0.0.1", port, &h, &cp);
    CHECK(tu_run_until(l, &g_accept_seen));
    tu_probe_t sp = {0};
    tcp_set_handlers(g_accepted, &h, &sp);
    CHECK(tcp_send(c, src, N) == 0);
    tcp_close(c, 1);
    for (int i = 0; i < 2000 && !sp.closed; i++) {
        loop_run_once(l, 5);
    }
    CHECK(sp.rx_total == N);
    CHECK(sp.closed && sp.last_err == 0);
    tcp_listener_close(g_ls);
    tu_pump(l, 10);
    loop_free(l);
}

int main(void)
{
    RUN(test_listen_port_and_connect);
    RUN(test_echo_exchange);
    RUN(test_one_mib_integrity);
    RUN(test_connection_refused);
    RUN(test_graceful_close_flushes_tx);
    return test_summary();
}
