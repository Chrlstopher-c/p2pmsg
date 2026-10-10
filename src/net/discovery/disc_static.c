/* net/discovery — parseur du fichier statique de pairs (prive au module).
 * Choix d'interpretation : nom > 64 octets = ligne rejetee entiere (LOG_W), pas tronquee.
 * Ligne longue (> 1024 octets) = rejetee, le reste du fichier est lu normalement. */
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "core/log/log.h"
#include "net/discovery/disc_internal.h"

#define STATIC_LINE_MAX 1024u

static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

static char *skip_blank(char *p)
{
    while (*p != '\0' && is_blank(*p)) {
        p++;
    }
    return p;
}

static char *take_token(char **cursor)
{
    char *start = skip_blank(*cursor);
    char *end = start;

    while (*end != '\0' && !is_blank(*end)) {
        end++;
    }
    if (*end != '\0') {
        *end = '\0';
        end++;
    }
    *cursor = end;
    return start;
}

static void trim_end(char *s)
{
    size_t n = strlen(s);

    while (n > 0 && is_blank(s[n - 1])) {
        s[--n] = '\0';
    }
}

static void reject(int lineno, const char *why)
{
    LOG_W(DISC_LOG, "static peers line=%d ignored: %s", lineno, why);
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int disc_parse_hex_key(const char *s, size_t len, uint8_t out[32])
{
    size_t i;

    if (!s || !out || len != 64) {
        return -EINVAL;
    }
    for (i = 0; i < 32; i++) {
        int hi = hex_nibble(s[2 * i]);
        int lo = hex_nibble(s[2 * i + 1]);

        if (hi < 0 || lo < 0) {
            return -EINVAL;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int parse_port(const char *s, uint16_t *out)
{
    unsigned long v = 0;
    size_t i;
    size_t n = strlen(s);

    if (n == 0 || n > 5) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        v = v * 10u + (unsigned long)(s[i] - '0');
    }
    if (v == 0 || v > 65535u) {
        return -1;
    }
    *out = (uint16_t)v;
    return 0;
}

static int is_ip_literal(const char *host, int v6)
{
    struct in_addr a4;
    struct in6_addr a6;

    if (v6) {
        return inet_pton(AF_INET6, host, &a6) == 1;
    }
    return inet_pton(AF_INET, host, &a4) == 1;
}

/* Localise l'hote et le ':' qui precede le port. Rejette IPv6 sans crochets et crochet non ferme. */
static int split_endpoint(const char *s, const char **host_src, size_t *hlen, const char **colon, int lineno)
{
    if (s[0] == '[') {
        const char *close = strchr(s, ']');

        if (!close || close[1] != ':') {
            reject(lineno, "IPv6 mal formee, attendu [addr]:port");
            return -1;
        }
        *host_src = s + 1;
        *hlen = (size_t)(close - *host_src);
        *colon = close + 1;
        return 0;
    }
    *colon = strrchr(s, ':');
    if (!*colon || *colon != strchr(s, ':')) {
        reject(lineno, "endpoint : a.b.c.d:port ou [v6]:port attendu");
        return -1;
    }
    *host_src = s;
    *hlen = (size_t)(*colon - s);
    return 0;
}

/* Accepte `a.b.c.d:port` et `[v6]:port`. */
static int parse_endpoint(const char *s, disc_peer *out, int lineno)
{
    const char *colon = NULL;
    const char *host_src = NULL;
    size_t hlen = 0;
    int v6 = s[0] == '[';

    if (split_endpoint(s, &host_src, &hlen, &colon, lineno) != 0) {
        return -1;
    }
    if (hlen == 0 || hlen >= DISC_HOST_MAX) {
        reject(lineno, "hote vide ou trop long");
        return -1;
    }
    memcpy(out->host, host_src, hlen);
    out->host[hlen] = '\0';
    if (!is_ip_literal(out->host, v6)) {
        reject(lineno, "hote non IP litterale");
        return -1;
    }
    if (parse_port(colon + 1, &out->port) != 0) {
        reject(lineno, "port invalide (1..65535)");
        return -1;
    }
    return 0;
}

static int parse_name(const char *rest, disc_peer *out, int lineno)
{
    size_t n = strlen(rest);

    if (n > DISC_NAME_MAX) {
        reject(lineno, "nom trop long (> 64 octets)");
        return -1;
    }
    memcpy(out->name, rest, n);
    out->name[n] = '\0';
    return 0;
}

/* Retour : 0 entree valide, 1 ligne vide ou commentaire (silencieux), -1 ligne rejetee (LOG_W emis). */
static int parse_line(char *line, int lineno, disc_peer *out)
{
    char *cursor;
    char *host_tok;
    char *key_tok;

    trim_end(line);
    cursor = skip_blank(line);
    if (*cursor == '\0' || *cursor == '#') {
        return 1;
    }
    host_tok = take_token(&cursor);
    key_tok = take_token(&cursor);
    if (*key_tok == '\0') {
        reject(lineno, "champs manquants (host:port pubkey [nom])");
        return -1;
    }
    if (parse_endpoint(host_tok, out, lineno) != 0) {
        return -1;
    }
    if (disc_parse_hex_key(key_tok, strlen(key_tok), out->pubkey) != 0) {
        reject(lineno, "pubkey hex invalide (64 caracteres attendus)");
        return -1;
    }
    if (parse_name(skip_blank(cursor), out, lineno) != 0) {
        return -1;
    }
    out->source = DISC_STATIC;
    return 0;
}

/* Lit la fin d'une ligne trop longue sans la retenir. 1 si la ligne etait complete, 0 si elle a ete jetee. */
static int has_full_line(const char *line, FILE *f)
{
    int c;

    if (strchr(line, '\n') || feof(f)) {
        return 1;
    }
    while ((c = fgetc(f)) != '\n' && c != EOF) {
        continue;
    }
    return 0;
}

int disc_parse_peers_file(const char *path, disc_peer *out, int max)
{
    FILE *f;
    char line[STATIC_LINE_MAX + 2];
    int n = 0;
    int lineno = 0;

    if (!path || !out || max < 0) {
        return -EINVAL;
    }
    f = fopen(path, "r");
    if (!f) {
        int err = errno;

        LOG_W(DISC_LOG, "static peers file unreadable: %s (errno=%d)", path, err);
        return -err;
    }
    while (fgets(line, sizeof line, f)) {
        lineno++;
        if (!has_full_line(line, f)) {
            reject(lineno, "line too long (> 1024 bytes)");
            continue;
        }
        if (n >= max) {
            LOG_W(DISC_LOG, "static peers: more than %d entries, rest ignored", max);
            break;
        }
        if (parse_line(line, lineno, &out[n]) == 0) {
            n++;
        }
    }
    fclose(f);
    return n;
}
