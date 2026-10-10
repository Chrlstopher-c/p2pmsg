/* ui/gtk : options CLI identiques au binaire p2pmsg (--dir --name --port --no-mdns --debug). */
#include <string.h>

#include "test.h"
#include "ui/gtk/ui_options.h"

static void test_defaults(void)
{
    char *argv[] = {"p2pmsg-gtk", NULL};
    ui_options o;

    CHECK_EQ_INT(ui_options_parse(1, argv, &o), 0);
    CHECK(o.dir == NULL);
    CHECK(o.name == NULL);
    CHECK_EQ_INT(o.port, 0);
    CHECK_EQ_INT(o.no_mdns, 0);
    CHECK_EQ_INT(o.debug, 0);
}

static void test_all_options(void)
{
    char *argv[] = {"p2pmsg-gtk", "--dir", "/tmp/a", "--name=bob", "--port", "47801",
                    "--no-mdns", "--debug", NULL};
    ui_options o;

    CHECK_EQ_INT(ui_options_parse(8, argv, &o), 0);
    CHECK(strcmp(o.dir, "/tmp/a") == 0);
    CHECK(strcmp(o.name, "bob") == 0);
    CHECK_EQ_INT(o.port, 47801);
    CHECK_EQ_INT(o.no_mdns, 1);
    CHECK_EQ_INT(o.debug, 1);
}

static void test_errors(void)
{
    char *unknown[] = {"p2pmsg-gtk", "--nope", NULL};
    char *badport[] = {"p2pmsg-gtk", "--port", "70000", NULL};
    char *nanport[] = {"p2pmsg-gtk", "--port", "abc", NULL};
    char *missing[] = {"p2pmsg-gtk", "--dir", NULL};
    char *positional[] = {"p2pmsg-gtk", "extra", NULL};
    ui_options o;

    CHECK_EQ_INT(ui_options_parse(2, unknown, &o) != 0, 1);
    CHECK_EQ_INT(ui_options_parse(3, badport, &o) != 0, 1);
    CHECK_EQ_INT(ui_options_parse(3, nanport, &o) != 0, 1);
    CHECK_EQ_INT(ui_options_parse(2, missing, &o) != 0, 1);
    CHECK_EQ_INT(ui_options_parse(2, positional, &o) != 0, 1);
}

int main(void)
{
    RUN(test_defaults);
    RUN(test_all_options);
    RUN(test_errors);
    return test_summary();
}
