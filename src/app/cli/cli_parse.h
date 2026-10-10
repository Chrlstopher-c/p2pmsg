/* app/cli — analyse pure des options et des lignes de saisie (sans I/O, testable). */
#ifndef P2P_APP_CLI_PARSE_H
#define P2P_APP_CLI_PARSE_H
#include <stdint.h>

#include "app/api/app.h"

#define CLI_HIST_DEFAULT 20
#define CLI_HIST_MAX     1000
#define CLI_PREFIX_MIN   8

typedef struct {
    const char *dir;
    const char *name;
    uint16_t    port;
    int         no_mdns;
    int         debug;
} cli_opts;

typedef enum { CLI_SEND, CLI_PEERS, CLI_TO, CLI_HIST, CLI_QUIT, CLI_EMPTY, CLI_BAD } cli_kind;

typedef struct {
    cli_kind    kind;
    const char *arg;    /* SEND : texte ; TO : préfixe ; sinon NULL */
    int         limit;  /* HIST : nombre de messages */
} cli_cmd;

typedef enum { CLI_MATCH_ONE, CLI_MATCH_NONE, CLI_MATCH_AMBIG, CLI_MATCH_BAD } cli_match;

/* 0 ou -EINVAL (option inconnue, valeur invalide). Réinitialisable : appelable plusieurs fois. */
int cli_parse_args(int argc, char **argv, cli_opts *out);
/* Modifie line (retire le \n final). Toujours 0. */
int cli_parse_line(char *line, cli_cmd *out);
/* Préfixe hex insensible à la casse, ≥ CLI_PREFIX_MIN caractères. */
cli_match cli_match_peer(const char *prefix, const app_peer *peers, int n, const app_peer **out);

#endif
