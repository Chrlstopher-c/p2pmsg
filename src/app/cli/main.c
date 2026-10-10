#define _GNU_SOURCE
/* app/cli : binaire p2pmsg. Pilote app/api depuis stdin ; événements stables sur stdout, erreurs sur stderr.
 * stdin fermé (EOF) : reste actif jusqu'à SIGINT/SIGTERM, pour un usage en arrière-plan. */
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app/api/app.h"
#include "app/cli/cli_parse.h"

#define APP_TEXT_LIMIT_CLI 4096
#define LINE_BUF (APP_TEXT_LIMIT_CLI + 64)
#define PEERS_MAX 256

typedef struct {
    app_t *app;
    char   to[APP_KEY_HEX];
    int    has_to;
} cli_ctx;

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static void out_line(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static void err_line(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("ERR ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static const char *state_name(app_peer_state st)
{
    switch (st) {
    case APP_CONNECTING: return "connecting";
    case APP_CONNECTED:  return "connected";
    default:             return "offline";
    }
}

static const char *status_name(app_msg_status st)
{
    switch (st) {
    case APP_MSG_DELIVERED: return "delivered";
    case APP_MSG_FAILED:    return "failed";
    case APP_MSG_RECEIVED:  return "received";
    default:                return "pending";
    }
}

static void print_peer(const app_peer *p)
{
    out_line("PEER %s %s %s\n", p->key, state_name(p->state), p->name);
}

static void on_peer(const app_peer *p, void *ud)
{
    (void)ud;
    print_peer(p);
}

static void on_message(const app_message *m, void *ud)
{
    (void)ud;
    out_line("RECV %s %016llx %s\n", m->peer, (unsigned long long)m->id, m->text);
}

static void on_status(const char *peer, uint64_t id, app_msg_status st, void *ud)
{
    (void)ud;
    out_line("STATUS %s %016llx %s\n", peer, (unsigned long long)id, status_name(st));
}

static void print_hist(const app_message *m, void *ud)
{
    (void)ud;
    out_line("HIST %s %016llx %s %s %s\n", m->peer, (unsigned long long)m->id,
             m->outgoing ? "out" : "in", status_name(m->status), m->text);
}

static void cmd_peers(cli_ctx *c)
{
    app_peer peers[PEERS_MAX];
    int n = app_peers(c->app, peers, PEERS_MAX);

    for (int i = 0; i < n; i++) {
        print_peer(&peers[i]);
    }
}

static void cmd_to(cli_ctx *c, const char *prefix)
{
    app_peer peers[PEERS_MAX];
    const app_peer *found = NULL;
    int n = app_peers(c->app, peers, PEERS_MAX);
    cli_match rc = cli_match_peer(prefix, peers, n, &found);

    if (rc == CLI_MATCH_ONE) {
        snprintf(c->to, sizeof c->to, "%s", found->key);
        c->has_to = 1;
        out_line("TO %s %s\n", found->key, found->name);
    } else if (rc == CLI_MATCH_AMBIG) {
        err_line("préfixe ambigu : %s", prefix);
    } else if (rc == CLI_MATCH_NONE) {
        err_line("pair inconnu : %s", prefix);
    } else {
        err_line("préfixe invalide (hex, ≥ %d car.) : %s", CLI_PREFIX_MIN, prefix);
    }
}

static void cmd_hist(cli_ctx *c, int limit)
{
    int rc;

    if (!c->has_to) {
        err_line("aucun pair courant (/to <clé>)");
        return;
    }
    rc = app_history(c->app, c->to, UINT64_MAX, limit, print_hist, NULL);
    if (rc != 0) {
        err_line("historique indisponible rc=%d", rc);
    }
}

static void cmd_send(cli_ctx *c, const char *text)
{
    uint64_t id = 0;
    int rc;

    if (!c->has_to) {
        err_line("aucun pair courant (/to <clé>)");
        return;
    }
    rc = app_send(c->app, c->to, text, &id);
    if (rc != 0) {
        err_line("envoi refusé rc=%d", rc);
        return;
    }
    out_line("SENT %s %016llx\n", c->to, (unsigned long long)id);
}

/* Retourne 1 si /quit. */
static int handle_line(cli_ctx *c, char *line)
{
    cli_cmd cmd;

    cli_parse_line(line, &cmd);
    switch (cmd.kind) {
    case CLI_QUIT:  return 1;
    case CLI_PEERS: cmd_peers(c); break;
    case CLI_TO:    cmd_to(c, cmd.arg); break;
    case CLI_HIST:  cmd_hist(c, cmd.limit); break;
    case CLI_SEND:  cmd_send(c, cmd.arg); break;
    case CLI_EMPTY: break;
    default:        err_line("commande inconnue"); break;
    }
    return 0;
}

/* Ligne trop longue : on consomme le reste jusqu'au \n sans l'envoyer en deux morceaux. */
static void discard_rest_of_line(void)
{
    int ch;

    do {
        ch = fgetc(stdin);
    } while (ch != '\n' && ch != EOF);
    err_line("ligne trop longue (max %d octets)", APP_TEXT_LIMIT_CLI);
}

static void wait_for_signal(void)
{
    while (!g_stop) {
        pause();
    }
}

static void run_stdin(cli_ctx *c)
{
    char line[LINE_BUF];

    while (!g_stop) {
        if (fgets(line, sizeof line, stdin) == NULL) {
            if (feof(stdin)) {
                wait_for_signal();
            }
            clearerr(stdin);
            continue;
        }
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            discard_rest_of_line();
            continue;
        }
        if (handle_line(c, line)) {
            return;
        }
    }
}

static void usage(void)
{
    fputs("usage: p2pmsg [--dir DIR] [--name NOM] [--port N] [--no-mdns] [--debug]\n", stderr);
}

int main(int argc, char **argv)
{
    cli_opts o;
    cli_ctx ctx;
    app_config cfg;
    app_events ev = {.on_peer = on_peer, .on_message = on_message, .on_status = on_status};

    if (cli_parse_args(argc, argv, &o) != 0) {
        usage();
        return 2;
    }
    cfg = (app_config){.dir = o.dir, .name = o.name, .listen_port = o.port,
                       .enable_mdns = !o.no_mdns, .log_debug = o.debug};
    memset(&ctx, 0, sizeof ctx);
    install_signals();
    ctx.app = app_new(&cfg, &ev, NULL);
    if (ctx.app == NULL) {
        err_line("démarrage impossible (voir p2pmsg.log du dossier)");
        return 1;
    }
    if (app_start(ctx.app) != 0) {
        err_line("boucle réseau impossible");
        app_free(ctx.app);
        return 1;
    }
    out_line("SELF %s %u\n", app_self_key(ctx.app), (unsigned)app_listen_port(ctx.app));
    run_stdin(&ctx);
    app_stop(ctx.app);
    app_free(ctx.app);
    return 0;
}
