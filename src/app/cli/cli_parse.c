/* app/cli : analyse des options (getopt_long) et des lignes (/peers /to /hist /quit, sinon envoi). */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "app/cli/cli_parse.h"

static const struct option LONG_OPTS[] = {
    {"dir", required_argument, NULL, 'd'},
    {"name", required_argument, NULL, 'n'},
    {"port", required_argument, NULL, 'p'},
    {"no-mdns", no_argument, NULL, 'm'},
    {"debug", no_argument, NULL, 'g'},
    {NULL, 0, NULL, 0},
};

static int parse_port(const char *s, uint16_t *out)
{
    char *end = NULL;
    unsigned long v;

    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v > UINT16_MAX) {
        return -EINVAL;
    }
    *out = (uint16_t)v;
    return 0;
}

int cli_parse_args(int argc, char **argv, cli_opts *out)
{
    int opt;

    memset(out, 0, sizeof *out);
    optind = 1;
    while ((opt = getopt_long(argc, argv, "", LONG_OPTS, NULL)) != -1) {
        switch (opt) {
        case 'd': out->dir = optarg; break;
        case 'n': out->name = optarg; break;
        case 'p':
            if (parse_port(optarg, &out->port) != 0) {
                return -EINVAL;
            }
            break;
        case 'm': out->no_mdns = 1; break;
        case 'g': out->debug = 1; break;
        default: return -EINVAL;
        }
    }
    return optind == argc ? 0 : -EINVAL;
}

static int parse_hist_limit(const char *s, int *out)
{
    char *end = NULL;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < 1 || v > CLI_HIST_MAX) {
        return -EINVAL;
    }
    *out = (int)v;
    return 0;
}

static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static void parse_slash(const char *s, cli_cmd *out)
{
    const char *arg;

    if (strcmp(s, "/peers") == 0) {
        out->kind = CLI_PEERS;
    } else if (strcmp(s, "/quit") == 0) {
        out->kind = CLI_QUIT;
    } else if (strncmp(s, "/to ", 4) == 0 && *skip_spaces(s + 4) != '\0') {
        out->kind = CLI_TO;
        out->arg = skip_spaces(s + 4);
    } else if (strcmp(s, "/hist") == 0) {
        out->kind = CLI_HIST;
        out->limit = CLI_HIST_DEFAULT;
    } else if (strncmp(s, "/hist ", 6) == 0) {
        arg = skip_spaces(s + 6);
        out->kind = parse_hist_limit(arg, &out->limit) == 0 ? CLI_HIST : CLI_BAD;
    } else {
        out->kind = CLI_BAD;
    }
}

int cli_parse_line(char *line, cli_cmd *out)
{
    const char *s;
    size_t len = strlen(line);

    memset(out, 0, sizeof *out);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
    s = line;
    if (*s == '\0') {
        out->kind = CLI_EMPTY;
    } else if (*s == '/') {
        parse_slash(s, out);
    } else {
        out->kind = CLI_SEND;
        out->arg = s;
    }
    return 0;
}

static int is_prefix_valid(const char *p)
{
    size_t n = strlen(p);

    if (n < CLI_PREFIX_MIN || n > APP_KEY_HEX - 1u) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (!isxdigit((unsigned char)p[i])) {
            return 0;
        }
    }
    return 1;
}

cli_match cli_match_peer(const char *prefix, const app_peer *peers, int n, const app_peer **out)
{
    int found = -1;
    int count = 0;

    if (!is_prefix_valid(prefix)) {
        return CLI_MATCH_BAD;
    }
    for (int i = 0; i < n; i++) {
        if (strncasecmp(peers[i].key, prefix, strlen(prefix)) == 0) {
            found = i;
            count++;
        }
    }
    if (count == 0) {
        return CLI_MATCH_NONE;
    }
    if (count > 1) {
        return CLI_MATCH_AMBIG;
    }
    *out = &peers[found];
    return CLI_MATCH_ONE;
}
