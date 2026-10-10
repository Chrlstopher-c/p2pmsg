/* Tests net/tcp : TCP_ETXFULL, close dans on_data, pair qui ferme, 50 connexions, IPv6. */
#include <errno.h>
#include <string.h>
#include "tcp_test_util.h"

static tcp_listener_t *g_ls;
static tcp_conn_t *g_accepted[64];
static int g_accepted_n;

static void on_accept(tcp_listener_t *ls, tcp_conn_t *c, void *ud)
{
    (void)ls;
    (void)ud;
    if (g_accepted_n < 64) {
        g_accepted[g_accepted_n] = c;
    }
    g_accepted_n++;
}

static void close_all_accepted(void)
{
    for (int i = 0; i < g_accepted_n && i < 64; i++) {
        tcp_close(g_accepted[i], 0);
    }
}

static void test_etxfull(void)
{
    enum { CHUNK = 64 * 1024 };
    static uint8_t chunk[CHUNK];
    memset(chunk, 1, CHUNK);
    loop_t *l = loop_new();
    g_accepted_n = 0;
    g_ls = tcp_listen(l, "127.0.0.1", 0, on_accept, NULL);
    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *c = tcp_connect(l, "127.0.0.1", tcp_listener_port(g_ls), &h, &cp);
    CHECK(tu_run_until(l, &cp.connects));
    int full = 0;
    for (int i = 0; i < 1024 && !full; i++) {
        if (tcp_send(c, chunk, CHUNK) == TCP_ETXFULL) {
            full = 1;
        }
    }
    CHECK(full);
    CHECK_EQ_INT(tcp_send(c, chunk, CHUNK), TCP_ETXFULL);
    tcp_close(c, 0);
    close_all_accepted();
    tcp_listener_close(g_ls);
    tu_pump(l, 20);
    loop_free(l);
}

static void on_data_closes(tcp_conn_t *c, buf_t *rx, void *ud)
{
    tu_probe_t *p = (tu_probe_t *)ud;
    p->data_calls++;
    buf_consume(rx, buf_readable(rx));
    tcp_close(c, 0);
}

static void test_close_in_on_data(void)
{
    loop_t *l = loop_new();
    g_accepted_n = 0;
    g_ls = tcp_listen(l, "127.0.0.1", 0, on_accept, NULL);
    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *c = tcp_connect(l, "127.0.0.1", tcp_listener_port(g_ls), &h, &cp);
    tu_pump(l, 20);
    CHECK(g_accepted_n == 1);
    tcp_handlers sh = { NULL, on_data_closes, tu_on_close };
    tu_probe_t sp = {0};
    tcp_set_handlers(g_accepted[0], &sh, &sp);
    tcp_send(c, "x", 1);
    tu_pump(l, 40);
    CHECK(sp.closes == 1);
    CHECK(sp.last_err == 0);
    tcp_listener_close(g_ls);
    tu_pump(l, 10);
    loop_free(l);
}

static void on_data_ignore(tcp_conn_t *c, buf_t *rx, void *ud)
{
    (void)c;
    (void)rx;
    (void)ud;
}

static void test_rx_full_enobufs(void)
{
    enum { CHUNK = 64 * 1024 };
    static uint8_t chunk[CHUNK];
    memset(chunk, 2, CHUNK);
    loop_t *l = loop_new();
    g_accepted_n = 0;
    g_ls = tcp_listen(l, "127.0.0.1", 0, on_accept, NULL);
    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *c = tcp_connect(l, "127.0.0.1", tcp_listener_port(g_ls), &h, &cp);
    tu_pump(l, 20);
    CHECK(g_accepted_n == 1);
    tcp_handlers sh = { NULL, on_data_ignore, tu_on_close };
    tu_probe_t sp = {0};
    tcp_set_handlers(g_accepted[0], &sh, &sp);
    for (int i = 0; i < 8; i++) {
        tcp_send(c, chunk, CHUNK);
    }
    CHECK(tu_run_until(l, &sp.closed));
    CHECK_EQ_INT(sp.last_err, -ENOBUFS);
    tcp_close(c, 0);
    tcp_listener_close(g_ls);
    tu_pump(l, 10);
    loop_free(l);
}

static void test_peer_close_gives_zero(void)
{
    loop_t *l = loop_new();
    g_accepted_n = 0;
    g_ls = tcp_listen(l, "127.0.0.1", 0, on_accept, NULL);
    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *c = tcp_connect(l, "127.0.0.1", tcp_listener_port(g_ls), &h, &cp);
    tu_pump(l, 20);
    tu_probe_t sp = {0};
    tcp_set_handlers(g_accepted[0], &h, &sp);
    tcp_close(c, 1);
    CHECK(tu_run_until(l, &sp.closed));
    CHECK(sp.closes == 1);
    CHECK_EQ_INT(sp.last_err, 0);
    tcp_listener_close(g_ls);
    tu_pump(l, 10);
    loop_free(l);
}

static void test_fifty_concurrent(void)
{
    loop_t *l = loop_new();
    g_accepted_n = 0;
    g_ls = tcp_listen(l, "127.0.0.1", 0, on_accept, NULL);
    uint16_t port = tcp_listener_port(g_ls);
    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *conns[50];
    for (int i = 0; i < 50; i++) {
        conns[i] = tcp_connect(l, "127.0.0.1", port, &h, &cp);
        CHECK(conns[i] != NULL);
    }
    for (int i = 0; i < 2000 && g_accepted_n < 50; i++) {
        loop_run_once(l, 5);
    }
    CHECK(g_accepted_n == 50);
    CHECK(cp.connects == 50);
    for (int i = 0; i < 50; i++) {
        tcp_close(conns[i], 0);
    }
    close_all_accepted();
    tcp_listener_close(g_ls);
    tu_pump(l, 20);
    loop_free(l);
}

static void test_ipv6_loopback(void)
{
    loop_t *l = loop_new();
    g_accepted_n = 0;
    g_ls = tcp_listen(l, "::1", 0, on_accept, NULL);
    CHECK(g_ls != NULL);
    if (g_ls == NULL) {
        loop_free(l);
        return;
    }
    tu_probe_t cp = {0};
    tcp_handlers h = tu_handlers();
    tcp_conn_t *c = tcp_connect(l, "::1", tcp_listener_port(g_ls), &h, &cp);
    CHECK(c != NULL);
    for (int i = 0; i < 1000 && (cp.connects == 0 || g_accepted_n == 0); i++) {
        loop_run_once(l, 5);
    }
    CHECK(cp.connects == 1);
    CHECK(g_accepted_n == 1);
    char addr[64] = {0};
    CHECK(tcp_peer_addr(c, addr, sizeof addr) != NULL);
    CHECK(strstr(addr, "::1") != NULL);
    tcp_close(c, 0);
    close_all_accepted();
    tcp_listener_close(g_ls);
    tu_pump(l, 10);
    loop_free(l);
}

static void test_invalid_host_null(void)
{
    loop_t *l = loop_new();
    tcp_handlers h = tu_handlers();
    tu_probe_t cp = {0};
    CHECK(tcp_connect(l, "not-an-ip", 1, &h, &cp) == NULL);
    CHECK(tcp_listen(l, "not-an-ip", 0, on_accept, NULL) == NULL);
    loop_free(l);
}

int main(void)
{
    RUN(test_etxfull);
    RUN(test_close_in_on_data);
    RUN(test_rx_full_enobufs);
    RUN(test_peer_close_gives_zero);
    RUN(test_fifty_concurrent);
    RUN(test_ipv6_loopback);
    RUN(test_invalid_host_null);
    return test_summary();
}
