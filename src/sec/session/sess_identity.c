/* sec/session : identité statique (clé Curve25519 persistée, fichier 0600) et encodage hex des clés. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <noise/protocol/constants.h>
#include <noise/protocol/dhstate.h>
#include <noise/protocol/errors.h>
#include "core/log/log.h"
#include "sec/session/session.h"

#define SESS_MOD "sec/session"
#define ID_MODE_MASK 0077u
#define HEX_DIGITS "0123456789abcdef"

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void sess_key_to_hex(const uint8_t key[SESS_KEY_LEN], char out[2 * SESS_KEY_LEN + 1])
{
    for (size_t i = 0; i < SESS_KEY_LEN; i++) {
        out[2 * i] = HEX_DIGITS[key[i] >> 4];
        out[2 * i + 1] = HEX_DIGITS[key[i] & 0x0f];
    }
    out[2 * SESS_KEY_LEN] = '\0';
}

int sess_key_from_hex(const char *hex, uint8_t key[SESS_KEY_LEN])
{
    if (!hex || !key || strlen(hex) != 2 * SESS_KEY_LEN) {
        return -EINVAL;
    }
    uint8_t tmp[SESS_KEY_LEN];
    for (size_t i = 0; i < SESS_KEY_LEN; i++) {
        int hi = hex_nibble(hex[2 * i]);
        int lo = hex_nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            explicit_bzero(tmp, sizeof(tmp));
            return -EINVAL;
        }
        tmp[i] = (uint8_t)((hi << 4) | lo);
    }
    memcpy(key, tmp, SESS_KEY_LEN);
    explicit_bzero(tmp, sizeof(tmp));
    return 0;
}

static int derive_public(const uint8_t priv[SESS_KEY_LEN], uint8_t pub[SESS_KEY_LEN])
{
    NoiseDHState *dh = NULL;
    int err = noise_dhstate_new_by_id(&dh, NOISE_DH_CURVE25519);
    if (err != NOISE_ERROR_NONE) {
        LOG_E(SESS_MOD, "dhstate init failed (%d)", err);
        return -EIO;
    }
    err = noise_dhstate_set_keypair_private(dh, priv, SESS_KEY_LEN);
    if (err == NOISE_ERROR_NONE) {
        err = noise_dhstate_get_public_key(dh, pub, SESS_KEY_LEN);
    }
    noise_dhstate_free(dh);
    if (err != NOISE_ERROR_NONE) {
        LOG_E(SESS_MOD, "public key derivation failed (%d)", err);
        return -EIO;
    }
    return 0;
}

static int generate_keypair(sess_identity *out)
{
    NoiseDHState *dh = NULL;
    int err = noise_dhstate_new_by_id(&dh, NOISE_DH_CURVE25519);
    if (err != NOISE_ERROR_NONE) {
        LOG_E(SESS_MOD, "dhstate init failed (%d)", err);
        return -EIO;
    }
    err = noise_dhstate_generate_keypair(dh);
    if (err == NOISE_ERROR_NONE) {
        err = noise_dhstate_get_keypair(dh, out->priv, SESS_KEY_LEN, out->pub, SESS_KEY_LEN);
    }
    noise_dhstate_free(dh);
    if (err != NOISE_ERROR_NONE) {
        LOG_E(SESS_MOD, "key generation failed (%d)", err);
        return -EIO;
    }
    return 0;
}

static int ensure_parent_dir(const char *path)
{
    char dir[PATH_MAX];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(dir)) {
        return -EINVAL;
    }
    memcpy(dir, path, n + 1);
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir) {
        return 0;
    }
    *slash = '\0';
    for (char *p = dir + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            char saved = *p;
            *p = '\0';
            if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
                LOG_E(SESS_MOD, "mkdir %s failed (%d)", dir, errno);
                return -errno;
            }
            *p = saved;
        }
        if (*p == '\0') {
            break;
        }
    }
    return 0;
}

static int read_existing(int fd, sess_identity *out)
{
    struct stat st;
    if (fstat(fd, &st) != 0) {
        return -errno;
    }
    if (!S_ISREG(st.st_mode)) {
        LOG_E(SESS_MOD, "identity file is not a regular file");
        return -EINVAL;
    }
    if ((st.st_mode & ID_MODE_MASK) != 0) {
        LOG_E(SESS_MOD, "identity file permissions too wide (must be 0600)");
        return -EPERM;
    }
    uint8_t priv[SESS_KEY_LEN];
    ssize_t r = read(fd, priv, sizeof(priv));
    if (r != (ssize_t)sizeof(priv)) {
        explicit_bzero(priv, sizeof(priv));
        LOG_E(SESS_MOD, "identity file has invalid size");
        return -EINVAL;
    }
    memcpy(out->priv, priv, SESS_KEY_LEN);
    explicit_bzero(priv, sizeof(priv));
    return derive_public(out->priv, out->pub);
}

static int create_existing(const char *path, sess_identity *out)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        LOG_E(SESS_MOD, "identity create failed (%d)", errno);
        return -errno;
    }
    int rc = generate_keypair(out);
    if (rc == 0 && write(fd, out->priv, SESS_KEY_LEN) != (ssize_t)SESS_KEY_LEN) {
        LOG_E(SESS_MOD, "identity write failed (%d)", errno);
        rc = -EIO;
    }
    if (close(fd) != 0 && rc == 0) {
        rc = -errno;
    }
    if (rc != 0) {
        unlink(path);
        explicit_bzero(out->priv, SESS_KEY_LEN);
    }
    return rc;
}

int sess_identity_load_or_create(const char *path, sess_identity *out)
{
    if (!path || !out) {
        return -EINVAL;
    }
    int rc = ensure_parent_dir(path);
    if (rc != 0) {
        return rc;
    }
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0) {
        rc = read_existing(fd, out);
        close(fd);
        return rc;
    }
    if (errno == ENOENT) {
        return create_existing(path, out);
    }
    LOG_E(SESS_MOD, "identity open failed (%d)", errno);
    return -errno;
}
