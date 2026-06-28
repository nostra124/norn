/**
 * @file store.c
 * @brief File-backed immutable object store for node-served content (FEAT-033).
 *        See store.h.
 */

#include "store.h"

#include <errno.h>
#include <sodium.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char HEXSET[] = "0123456789abcdef";

static void hex32(const unsigned char in[32], char out[NORND_STORE_HASH_HEX]) {
    for (int i = 0; i < 32; i++) {
        out[i * 2] = HEXSET[in[i] >> 4];
        out[i * 2 + 1] = HEXSET[in[i] & 0xf];
    }
}

int nornd_store_init(nornd_store_t *s, const char *root) {
    if (!s || !root) return -1;
    size_t n = strlen(root);
    if (n == 0 || n >= sizeof(s->root)) return -1;
    memcpy(s->root, root, n + 1);
    s->rootlen = n;
    if (mkdir(root, 0700) != 0 && errno != EEXIST) return -1;
    return 0;
}

int nornd_store_put(const nornd_store_t *s, const unsigned char *data,
                    size_t len, char *hash_out) {
    if (!s || !data || !hash_out) return -1;
    unsigned char digest[crypto_hash_sha256_BYTES]; /* 32 */
    crypto_hash_sha256(digest, data, len);
    char hex[NORND_STORE_HASH_HEX + 1];
    hex32(digest, hex);
    hex[NORND_STORE_HASH_HEX] = '\0';

    char path[600];
    int pn = snprintf(path, sizeof(path), "%s/%s", s->root, hex);
    if (pn < 0 || (size_t)pn >= sizeof(path)) /* LCOV_EXCL_BR_LINE: root bounded */
        return -1;                            /* LCOV_EXCL_LINE */

    /* Content-addressed and write-once: skip the write if it already exists. */
    struct stat st;
    if (stat(path, &st) != 0) {
        FILE *f = fopen(path, "wb");
        if (!f) return -1; /* LCOV_EXCL_BR_LINE: writable temp root in tests */
        if (len && fwrite(data, 1, len, f) != len) { /* LCOV_EXCL_BR_LINE */
            fclose(f);     /* LCOV_EXCL_LINE */
            return -1;     /* LCOV_EXCL_LINE */
        }
        fclose(f);
    }
    memcpy(hash_out, hex, NORND_STORE_HASH_HEX + 1);
    return 0;
}

int nornd_store_stat(const nornd_store_t *s, const char *hash, size_t hlen,
                     char *path_out, size_t pathcap, uint64_t *len_out) {
    if (!s || !hash || hlen != NORND_STORE_HASH_HEX) return -1;
    for (size_t i = 0; i < hlen; i++)
        if (!memchr(HEXSET, hash[i], sizeof(HEXSET) - 1)) return -1;

    char path[600];
    int pn = snprintf(path, sizeof(path), "%s/%.*s", s->root, (int)hlen, hash);
    if (pn < 0 || (size_t)pn >= sizeof(path)) /* LCOV_EXCL_BR_LINE: root bounded */
        return -1;                            /* LCOV_EXCL_LINE */

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    if (path_out) {
        if ((size_t)pn >= pathcap) return -1;
        memcpy(path_out, path, (size_t)pn + 1);
    }
    if (len_out) *len_out = (uint64_t)st.st_size;
    return 0;
}
