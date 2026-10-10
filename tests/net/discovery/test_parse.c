/* Tests du parseur de fichier statique : cas valides, bornes, et tous les rejets (ligne ignoree, jamais fatale). */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "net/discovery/discovery.h"

#define PARSE_FILE "disc_parse_test.txt"
#define K64 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define MAX_OUT 8

static int parse_text(const char *text, disc_peer *out, int max)
{
    FILE *f = fopen(PARSE_FILE, "wb");
    int n;

    if (!f) {
        return -1000;
    }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    n = disc_parse_peers_file(PARSE_FILE, out, max);
    remove(PARSE_FILE);
    return n;
}

static int count_of(const char *text)
{
    disc_peer out[MAX_OUT];

    return parse_text(text, out, MAX_OUT);
}

static void test_ipv4_fields(void)
{
    disc_peer p[MAX_OUT];
    int n = parse_text("192.0.2.10:47800 " K64 " alice\n", p, MAX_OUT);

    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(p[0].host, "192.0.2.10") == 0);
    CHECK_EQ_INT(p[0].port, 47800);
    CHECK(strcmp(p[0].name, "alice") == 0);
    CHECK_EQ_INT(p[0].pubkey[0], 0x01);
    CHECK_EQ_INT(p[0].pubkey[31], 0xef);
    CHECK_EQ_INT(p[0].source, DISC_STATIC);
}

static void test_ipv6_bracketed(void)
{
    disc_peer p[MAX_OUT];
    int n = parse_text("[::1]:47800 " K64 " bob\n", p, MAX_OUT);

    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(p[0].host, "::1") == 0);
    CHECK_EQ_INT(p[0].port, 47800);
}

static void test_crlf_line_endings(void)
{
    disc_peer p[MAX_OUT];
    int n = parse_text("192.0.2.1:1 " K64 " carl\r\n192.0.2.2:2 " K64 " dora\r\n", p, MAX_OUT);

    CHECK_EQ_INT(n, 2);
    CHECK(strcmp(p[0].name, "carl") == 0);
    CHECK(strcmp(p[1].name, "dora") == 0);
}

static void test_spaces_and_tabs(void)
{
    disc_peer p[MAX_OUT];
    int n = parse_text("  \t192.0.2.2:5000\t " K64 "    dan  \n", p, MAX_OUT);

    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(p[0].host, "192.0.2.2") == 0);
    CHECK(strcmp(p[0].name, "dan") == 0);
}

static void test_comments_and_blank_lines(void)
{
    CHECK_EQ_INT(count_of("# commentaire\n\n   \n# 198.51.100.4:1 " K64 " x\n"), 0);
}

static void test_missing_name_is_valid(void)
{
    disc_peer p[MAX_OUT];
    int n = parse_text("192.0.2.3:9 " K64 "\n", p, MAX_OUT);

    CHECK_EQ_INT(n, 1);
    CHECK(p[0].name[0] == '\0');
}

static void test_uppercase_hex_accepted(void)
{
    disc_peer p[MAX_OUT];
    int n = parse_text("192.0.2.3:9 0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\n", p, MAX_OUT);

    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(p[0].pubkey[1], 0x23);
}

static void test_name_with_spaces(void)
{
    disc_peer p[MAX_OUT];
    int n = parse_text("192.0.2.4:9 " K64 " Jean Paul\n", p, MAX_OUT);

    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(p[0].name, "Jean Paul") == 0);
}

static void test_port_bounds_accepted(void)
{
    CHECK_EQ_INT(count_of("192.0.2.5:1 " K64 " a\n192.0.2.5:65535 " K64 " b\n"), 2);
}

static void test_port_zero_rejected(void)
{
    CHECK_EQ_INT(count_of("192.0.2.5:0 " K64 " a\n"), 0);
}

static void test_port_too_large_rejected(void)
{
    CHECK_EQ_INT(count_of("192.0.2.5:65536 " K64 " a\n"), 0);
}

static void test_port_not_numeric_rejected(void)
{
    CHECK_EQ_INT(count_of("192.0.2.5:47a00 " K64 " a\n"), 0);
    CHECK_EQ_INT(count_of("192.0.2.5: " K64 " a\n"), 0);
}

static void test_hex_wrong_length_rejected(void)
{
    CHECK_EQ_INT(count_of("192.0.2.6:9 " K64 "0 x\n"), 0);
    CHECK_EQ_INT(count_of("192.0.2.6:9 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde x\n"), 0);
}

static void test_hex_invalid_char_rejected(void)
{
    CHECK_EQ_INT(count_of("192.0.2.6:9 g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef x\n"), 0);
}

static void test_name_65_rejected_64_accepted(void)
{
    char name65[66];
    char line[256];

    memset(name65, 'n', 65);
    name65[65] = '\0';
    snprintf(line, sizeof line, "192.0.2.7:9 " K64 " %s\n", name65);
    CHECK_EQ_INT(count_of(line), 0);
    name65[64] = '\0';
    snprintf(line, sizeof line, "192.0.2.7:9 " K64 " %s\n", name65);
    CHECK_EQ_INT(count_of(line), 1);
}

static void test_hostname_rejected(void)
{
    CHECK_EQ_INT(count_of("example.com:47800 " K64 " a\n"), 0);
}

static void test_ipv6_without_brackets_rejected(void)
{
    CHECK_EQ_INT(count_of("::1:47800 " K64 " a\n"), 0);
}

static void test_unclosed_bracket_rejected(void)
{
    CHECK_EQ_INT(count_of("[::1:47800 " K64 " a\n"), 0);
}

static void test_out_of_range_octet_rejected(void)
{
    CHECK_EQ_INT(count_of("256.1.1.1:47800 " K64 " a\n"), 0);
}

static void test_missing_port_or_key_rejected(void)
{
    CHECK_EQ_INT(count_of("192.0.2.8 " K64 " x\n"), 0);
    CHECK_EQ_INT(count_of("192.0.2.8:9\n"), 0);
}

static void test_overlong_line_skipped_rest_read(void)
{
    char *text = malloc(2048);
    disc_peer p[MAX_OUT];
    int n;

    if (!text) {
        CHECK(0);
        return;
    }
    memset(text, 'x', 1100);
    text[1100] = '\n';
    text[1101] = '\0';
    strcat(text, "192.0.2.9:9 " K64 " ok\n");
    n = parse_text(text, p, MAX_OUT);
    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(p[0].name, "ok") == 0);
    free(text);
}

static void test_mixed_valid_and_invalid(void)
{
    disc_peer p[MAX_OUT];
    int n = parse_text("bad\n192.0.2.1:1 " K64 " a\n192.0.2.2:0 " K64 " b\n192.0.2.3:3 " K64 " c\n", p, MAX_OUT);

    CHECK_EQ_INT(n, 2);
    CHECK(strcmp(p[0].name, "a") == 0);
    CHECK(strcmp(p[1].name, "c") == 0);
}

static void test_last_line_without_newline(void)
{
    CHECK_EQ_INT(count_of("192.0.2.1:1 " K64 " last"), 1);
}

static void test_missing_file_returns_errno(void)
{
    disc_peer p[MAX_OUT];

    CHECK_EQ_INT(disc_parse_peers_file("/nonexistent/disc_peers.txt", p, MAX_OUT), -ENOENT);
}

int main(void)
{
    RUN(test_ipv4_fields);
    RUN(test_ipv6_bracketed);
    RUN(test_crlf_line_endings);
    RUN(test_spaces_and_tabs);
    RUN(test_comments_and_blank_lines);
    RUN(test_missing_name_is_valid);
    RUN(test_uppercase_hex_accepted);
    RUN(test_name_with_spaces);
    RUN(test_port_bounds_accepted);
    RUN(test_port_zero_rejected);
    RUN(test_port_too_large_rejected);
    RUN(test_port_not_numeric_rejected);
    RUN(test_hex_wrong_length_rejected);
    RUN(test_hex_invalid_char_rejected);
    RUN(test_name_65_rejected_64_accepted);
    RUN(test_hostname_rejected);
    RUN(test_ipv6_without_brackets_rejected);
    RUN(test_unclosed_bracket_rejected);
    RUN(test_out_of_range_octet_rejected);
    RUN(test_missing_port_or_key_rejected);
    RUN(test_overlong_line_skipped_rest_read);
    RUN(test_mixed_valid_and_invalid);
    RUN(test_last_line_without_newline);
    RUN(test_missing_file_returns_errno);
    return test_summary();
}
