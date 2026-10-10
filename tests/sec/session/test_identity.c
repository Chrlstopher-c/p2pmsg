/* sec/session : identité (création 0600, chargement, refus de permissions larges, symlink) et hex. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "test.h"
#include "sec/session/session.h"

#define ID_PATH_MAX 512u

static char g_dir[ID_PATH_MAX];

static void dir_make(void)
{
    snprintf(g_dir, sizeof(g_dir), "/tmp/p2pid-XXXXXX");
    CHECK(mkdtemp(g_dir) != NULL);
}

static void path_of(char *out, size_t n, const char *name)
{
    size_t dlen = strlen(g_dir);
    size_t nlen = strlen(name);
    CHECK(dlen + 1 + nlen < n);
    memcpy(out, g_dir, dlen);
    out[dlen] = '/';
    memcpy(out + dlen + 1, name, nlen + 1);
}

static void dir_clean(void)
{
    char p[ID_PATH_MAX + 64];
    const char *names[] = { "id.key", "link.key", "target.key", "sub/deep/id.key", "sub/deep", "sub" };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        path_of(p, sizeof(p), names[i]);
        unlink(p);
        rmdir(p);
    }
    rmdir(g_dir);
}

static void test_hex_round_trip(void)
{
    uint8_t key[SESS_KEY_LEN];
    uint8_t back[SESS_KEY_LEN];
    char hex[2 * SESS_KEY_LEN + 1];
    size_t i;

    for (i = 0; i < SESS_KEY_LEN; ++i) {
        key[i] = (uint8_t)(i * 7u + 1u);
    }
    sess_key_to_hex(key, hex);
    CHECK_EQ_INT(strlen(hex), 2 * SESS_KEY_LEN);
    CHECK_EQ_INT(hex[0], '0');
    CHECK_EQ_INT(hex[1], '1');
    CHECK_EQ_INT(sess_key_from_hex(hex, back), 0);
    CHECK_EQ_MEM(key, back, SESS_KEY_LEN);
    hex[0] = 'F';
    hex[1] = 'F';
    CHECK_EQ_INT(sess_key_from_hex(hex, back), 0);
    CHECK_EQ_INT(back[0], 0xFF);
}

static void test_hex_rejects(void)
{
    uint8_t key[SESS_KEY_LEN];
    char hex[2 * SESS_KEY_LEN + 1];

    memset(hex, '0', sizeof(hex));
    hex[2 * SESS_KEY_LEN] = '\0';
    hex[5] = 'g';
    CHECK_EQ_INT(sess_key_from_hex(hex, key), -EINVAL);
    CHECK_EQ_INT(sess_key_from_hex("abcd", key), -EINVAL);
    CHECK_EQ_INT(sess_key_from_hex(NULL, key), -EINVAL);
}

static void test_create_and_load(void)
{
    char p[ID_PATH_MAX];
    sess_identity a;
    sess_identity b;
    struct stat st;

    dir_make();
    path_of(p, sizeof(p), "id.key");
    CHECK_EQ_INT(sess_identity_load_or_create(p, &a), 0);
    CHECK_EQ_INT(stat(p, &st), 0);
    CHECK_EQ_INT(st.st_mode & 0777, 0600);
    CHECK_EQ_INT(st.st_size, SESS_KEY_LEN);
    CHECK_EQ_INT(sess_identity_load_or_create(p, &b), 0);
    CHECK_EQ_MEM(a.priv, b.priv, SESS_KEY_LEN);
    CHECK_EQ_MEM(a.pub, b.pub, SESS_KEY_LEN);
    dir_clean();
}

static void test_creates_missing_directories(void)
{
    char p[ID_PATH_MAX];
    sess_identity a;

    dir_make();
    path_of(p, sizeof(p), "sub/deep/id.key");
    CHECK_EQ_INT(sess_identity_load_or_create(p, &a), 0);
    dir_clean();
}

static void test_refuses_wide_permissions(void)
{
    char p[ID_PATH_MAX];
    sess_identity a;

    dir_make();
    path_of(p, sizeof(p), "id.key");
    CHECK_EQ_INT(sess_identity_load_or_create(p, &a), 0);
    CHECK_EQ_INT(chmod(p, 0644), 0);
    CHECK_EQ_INT(sess_identity_load_or_create(p, &a), -EPERM);
    CHECK_EQ_INT(chmod(p, 0640), 0);
    CHECK_EQ_INT(sess_identity_load_or_create(p, &a), -EPERM);
    CHECK_EQ_INT(chmod(p, 0400), 0);
    CHECK_EQ_INT(sess_identity_load_or_create(p, &a), 0);
    dir_clean();
}

static void test_refuses_symlink(void)
{
    char p[ID_PATH_MAX];
    char t[ID_PATH_MAX];
    sess_identity a;

    dir_make();
    path_of(p, sizeof(p), "link.key");
    path_of(t, sizeof(t), "target.key");
    CHECK_EQ_INT(sess_identity_load_or_create(t, &a), 0);
    CHECK_EQ_INT(symlink(t, p), 0);
    CHECK(sess_identity_load_or_create(p, &a) < 0);
    unlink(p);
    unlink(t);
    dir_clean();
}

int main(void)
{
    RUN(test_hex_round_trip);
    RUN(test_hex_rejects);
    RUN(test_create_and_load);
    RUN(test_creates_missing_directories);
    RUN(test_refuses_wide_permissions);
    RUN(test_refuses_symlink);
    return test_summary();
}
