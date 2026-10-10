#define _GNU_SOURCE
/* app/api : validation des arguments, permissions du dossier, port occupé, envoi avant démarrage. */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "app/api/app.h"
#include "app_test_util.h"
#include "test.h"

#define VALID_HEX "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"

static app_t *open_app(const char *dir, uint16_t port)
{
    app_config cfg = {.dir = dir, .name = "t", .listen_port = port, .enable_mdns = 0, .log_debug = 0};
    return app_new(&cfg, NULL, NULL);
}

static void test_send_validation(void)
{
    char dir[64];
    uint64_t id = 0;
    app_t *a;

    CHECK_EQ_INT(test_tmpdir(dir, sizeof dir), 0);
    a = open_app(dir, test_port());
    CHECK(a != NULL);
    if (a == NULL) {
        test_rmdir_tree(dir);
        return;
    }
    CHECK_EQ_INT(app_send(a, "zz", "x", &id), -EINVAL);
    CHECK_EQ_INT(app_send(a, "0011", "x", &id), -EINVAL);
    CHECK_EQ_INT(app_send(a, VALID_HEX, "", &id), -EINVAL);
    CHECK_EQ_INT(app_send(a, VALID_HEX, NULL, &id), -EINVAL);
    {
        char big[4098];
        memset(big, 'a', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        CHECK_EQ_INT(app_send(a, VALID_HEX, big, &id), -EINVAL);
    }
    CHECK_EQ_INT(app_send(a, VALID_HEX, "hi", &id), 0);
    CHECK(id != 0);
    CHECK_EQ_INT(strlen(app_self_key(a)), 64);
    app_free(a);
    test_rmdir_tree(dir);
}

static void test_dir_modes(void)
{
    char root[64], dir[96], path[160];
    struct stat st;
    app_t *a;

    CHECK_EQ_INT(test_tmpdir(root, sizeof root), 0);
    snprintf(dir, sizeof dir, "%s/data", root);
    a = open_app(dir, test_port());
    CHECK(a != NULL);
    CHECK_EQ_INT(stat(dir, &st), 0);
    CHECK_EQ_INT(st.st_mode & 0777, 0700);
    snprintf(path, sizeof path, "%s/identity.key", dir);
    CHECK_EQ_INT(stat(path, &st), 0);
    CHECK_EQ_INT(st.st_mode & 0777, 0600);
    app_free(a);
    test_rmdir_tree(dir);
    rmdir(root);
}

static void test_port_busy(void)
{
    char dir1[64], dir2[64];
    uint16_t port = test_port();
    app_t *a, *b;

    CHECK_EQ_INT(test_tmpdir(dir1, sizeof dir1), 0);
    CHECK_EQ_INT(test_tmpdir(dir2, sizeof dir2), 0);
    a = open_app(dir1, port);
    CHECK(a != NULL);
    b = open_app(dir2, port);
    CHECK(b == NULL);
    app_free(b);
    app_free(a);
    test_rmdir_tree(dir1);
    test_rmdir_tree(dir2);
}

static void test_null_handles(void)
{
    CHECK(app_new(NULL, NULL, NULL) == NULL);
    app_free(NULL);
    app_stop(NULL);
    CHECK_EQ_INT(app_start(NULL), -EINVAL);
}

int main(void)
{
    RUN(test_send_validation);
    RUN(test_dir_modes);
    RUN(test_port_busy);
    RUN(test_null_handles);
    return test_summary();
}
