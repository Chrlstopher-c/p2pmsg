/* ui/gtk — analyse getopt_long des options, identique au CLI (--dir --name --port --no-mdns --debug). */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "ui/gtk/ui_options.h"

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

static int apply_option(int opt, const char *arg, ui_options *out)
{
    switch (opt) {
    case 'd': out->dir = arg; return 0;
    case 'n': out->name = arg; return 0;
    case 'p': return parse_port(arg, &out->port);
    case 'm': out->no_mdns = 1; return 0;
    case 'g': out->debug = 1; return 0;
    default:  return -EINVAL;
    }
}

int ui_options_parse(int argc, char **argv, ui_options *out)
{
    int opt;

    memset(out, 0, sizeof *out);
    optind = 1;
    while ((opt = getopt_long(argc, argv, "", LONG_OPTS, NULL)) != -1) {
        if (apply_option(opt, optarg, out) != 0) {
            return -EINVAL;
        }
    }
    return optind == argc ? 0 : -EINVAL;
}
