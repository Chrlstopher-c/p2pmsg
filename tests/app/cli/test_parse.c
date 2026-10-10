#define _GNU_SOURCE
/* app/cli : analyse des options et des lignes de saisie (cas nominaux, bornes, erreurs). */
#include <errno.h>
#include <string.h>

#include "app/cli/cli_parse.h"
#include "test.h"

static void test_args_ok(void)
{
    char *argv[] = {"p2pmsg", "--dir", "/x", "--name=bob", "--port", "47801", "--no-mdns", "--debug"};
    cli_opts o;

    CHECK_EQ_INT(cli_parse_args(8, argv, &o), 0);
    CHECK(strcmp(o.dir, "/x") == 0);
    CHECK(strcmp(o.name, "bob") == 0);
    CHECK_EQ_INT(o.port, 47801);
    CHECK_EQ_INT(o.no_mdns, 1);
    CHECK_EQ_INT(o.debug, 1);
}

static void test_args_errors(void)
{
    cli_opts o;
    char *bad_port[] = {"p2pmsg", "--port", "70000"};
    char *bad_num[] = {"p2pmsg", "--port", "12a"};
    char *unknown[] = {"p2pmsg", "--zork"};
    char *extra[] = {"p2pmsg", "positionnel"};

    CHECK_EQ_INT(cli_parse_args(3, bad_port, &o), -EINVAL);
    CHECK_EQ_INT(cli_parse_args(3, bad_num, &o), -EINVAL);
    CHECK_EQ_INT(cli_parse_args(2, unknown, &o), -EINVAL);
    CHECK_EQ_INT(cli_parse_args(2, extra, &o), -EINVAL);
}

static void check_line(const char *in, cli_kind kind, const char *arg, int limit)
{
    char buf[256];
    cli_cmd c;

    snprintf(buf, sizeof buf, "%s", in);
    CHECK_EQ_INT(cli_parse_line(buf, &c), 0);
    CHECK_EQ_INT(c.kind, kind);
    if (arg != NULL) {
        CHECK(c.arg != NULL && strcmp(c.arg, arg) == 0);
    }
    if (limit >= 0) {
        CHECK_EQ_INT(c.limit, limit);
    }
}

static void test_lines(void)
{
    check_line("/peers", CLI_PEERS, NULL, -1);
    check_line("/quit\r\n", CLI_QUIT, NULL, -1);
    check_line("", CLI_EMPTY, NULL, -1);
    check_line("hello world", CLI_SEND, "hello world", -1);
    check_line("/to   abcdef0123", CLI_TO, "abcdef0123", -1);
    check_line("/to", CLI_BAD, NULL, -1);
    check_line("/hist", CLI_HIST, NULL, CLI_HIST_DEFAULT);
    check_line("/hist 5", CLI_HIST, NULL, 5);
    check_line("/hist 0", CLI_BAD, NULL, -1);
    check_line("/hist 1001", CLI_BAD, NULL, -1);
    check_line("/nope", CLI_BAD, NULL, -1);
}

static void test_match(void)
{
    app_peer peers[3];
    const app_peer *p = NULL;

    memset(peers, 0, sizeof peers);
    strcpy(peers[0].key, "aaaaaaaa11111111");
    strcpy(peers[1].key, "aaaaaaaa22222222");
    strcpy(peers[2].key, "bbbbbbbb33333333");
    CHECK_EQ_INT(cli_match_peer("bbbbbbbb", peers, 3, &p), CLI_MATCH_ONE);
    CHECK(p == &peers[2]);
    CHECK_EQ_INT(cli_match_peer("AAAAAAAA1", peers, 3, &p), CLI_MATCH_ONE);
    CHECK(p == &peers[0]);
    CHECK_EQ_INT(cli_match_peer("aaaaaaaa", peers, 3, &p), CLI_MATCH_AMBIG);
    CHECK_EQ_INT(cli_match_peer("cccccccc", peers, 3, &p), CLI_MATCH_NONE);
    CHECK_EQ_INT(cli_match_peer("bbbb", peers, 3, &p), CLI_MATCH_BAD);
    CHECK_EQ_INT(cli_match_peer("zzzzzzzz", peers, 3, &p), CLI_MATCH_BAD);
}

int main(void)
{
    RUN(test_args_ok);
    RUN(test_args_errors);
    RUN(test_lines);
    RUN(test_match);
    return test_summary();
}
