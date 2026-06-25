/**
 * @file keydir.c
 * @brief Fleet key directory (FEAT-031). See keydir.h.
 */

#include "keydir.h"
#include "bencode.h"

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEYDIR_MAX_CHUNKS 64 /* 64 * 3072 ≈ 196 KiB of armored GPG */

static void hex_id(const unsigned char id[NORND_PUBKEY], char out[2 * NORND_PUBKEY + 1]) {
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < NORND_PUBKEY; i++) {
        out[2 * i] = H[id[i] >> 4];
        out[2 * i + 1] = H[id[i] & 0xf];
    }
    out[2 * NORND_PUBKEY] = '\0';
}

int nornd_keydir_ssh_key(const unsigned char id[NORND_PUBKEY], char *out, size_t cap) {
    char hex[2 * NORND_PUBKEY + 1];
    hex_id(id, hex);
    int n = snprintf(out, cap, "peer/%s/ssh", hex);
    return ((size_t)n < cap) ? n : -1;
}

int nornd_keydir_gpg_key(const unsigned char id[NORND_PUBKEY], char *out, size_t cap) {
    char hex[2 * NORND_PUBKEY + 1];
    hex_id(id, hex);
    int n = snprintf(out, cap, "peer/%s/gpg", hex);
    return ((size_t)n < cap) ? n : -1;
}

int nornd_keydir_gpg_chunk_key(const unsigned char id[NORND_PUBKEY], int idx,
                               char *out, size_t cap) {
    char hex[2 * NORND_PUBKEY + 1];
    hex_id(id, hex);
    int n = snprintf(out, cap, "peer/%s/gpg/%d", hex, idx);
    return ((size_t)n < cap) ? n : -1;
}

int nornd_keydir_put_ssh(const nornd_backend_t *be,
                         const unsigned char id[NORND_PUBKEY], const char *sshline) {
    char key[128];
    if (nornd_keydir_ssh_key(id, key, sizeof(key)) < 0) return -1; /* LCOV_EXCL_BR_LINE */
    return be->put(be->ctx, (const unsigned char *)key, strlen(key),
                   (const unsigned char *)sshline, strlen(sshline));
}

int nornd_keydir_get_ssh(const nornd_backend_t *be,
                         const unsigned char id[NORND_PUBKEY], char *out, size_t cap) {
    char key[128];
    if (nornd_keydir_ssh_key(id, key, sizeof(key)) < 0) return -1; /* LCOV_EXCL_BR_LINE */
    int n = be->get(be->ctx, (const unsigned char *)key, strlen(key),
                    (unsigned char *)out, cap - 1);
    if (n < 0) return -1;
    out[n] = '\0';
    return n;
}

int nornd_keydir_put_gpg(const nornd_backend_t *be,
                         const unsigned char id[NORND_PUBKEY],
                         const unsigned char *armor, size_t len) {
    if (len == 0 || len > (size_t)NORND_KEYDIR_CHUNK * KEYDIR_MAX_CHUNKS)
        return -1;
    int n = (int)((len + NORND_KEYDIR_CHUNK - 1) / NORND_KEYDIR_CHUNK);
    for (int i = 0; i < n; i++) {
        char ck[160];
        if (nornd_keydir_gpg_chunk_key(id, i, ck, sizeof(ck)) < 0) return -1; /* LCOV_EXCL_BR_LINE */
        size_t off = (size_t)i * NORND_KEYDIR_CHUNK;
        size_t clen = len - off < NORND_KEYDIR_CHUNK ? len - off : NORND_KEYDIR_CHUNK;
        if (be->put(be->ctx, (const unsigned char *)ck, strlen(ck), armor + off,
                    clen) != 0)
            return -1;
    }
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, armor, len);

    /* Manifest {len, n, sha256} written last so readers gate on it. */
    bencode_value_t *d = bencode_dict_new();
    if (!d) return -1; /* LCOV_EXCL_BR_LINE: malloc */
    bencode_dict_add(d, "len", bencode_int_new((int64_t)len));
    bencode_dict_add(d, "n", bencode_int_new(n));
    bencode_dict_add(d, "sha256", bencode_string_new((const char *)hash, sizeof(hash)));
    size_t mlen = 0;
    char *enc = bencode_encode(d, &mlen);
    bencode_free(d);
    if (!enc) return -1; /* LCOV_EXCL_BR_LINE: encode of valid tree never fails */
    char mk[128];
    if (nornd_keydir_gpg_key(id, mk, sizeof(mk)) < 0) { /* LCOV_EXCL_BR_LINE */
        free(enc);                                      /* LCOV_EXCL_LINE */
        return -1;                                      /* LCOV_EXCL_LINE */
    }
    int rc = be->put(be->ctx, (const unsigned char *)mk, strlen(mk),
                     (const unsigned char *)enc, mlen);
    free(enc);
    return rc == 0 ? 0 : -1;
}

int nornd_keydir_get_gpg(const nornd_backend_t *be,
                         const unsigned char id[NORND_PUBKEY], unsigned char *out,
                         size_t cap) {
    char mk[128];
    if (nornd_keydir_gpg_key(id, mk, sizeof(mk)) < 0) return -1; /* LCOV_EXCL_BR_LINE */
    unsigned char mbuf[512];
    int mn = be->get(be->ctx, (const unsigned char *)mk, strlen(mk), mbuf, sizeof(mbuf));
    if (mn < 0) return -1;

    size_t pos = 0;
    bencode_value_t *d = bencode_decode((const char *)mbuf, (size_t)mn, &pos);
    if (!d) return -1;
    bencode_value_t *lv = bencode_dict_get(d, "len");
    bencode_value_t *nv = bencode_dict_get(d, "n");
    bencode_value_t *sv = bencode_dict_get(d, "sha256");
    if (!lv || lv->type != BENCODE_INT || !nv || nv->type != BENCODE_INT ||
        !sv || sv->type != BENCODE_STRING ||
        sv->val.str_val.len != crypto_hash_sha256_BYTES) {
        bencode_free(d);
        return -1;
    }
    int64_t total = lv->val.int_val;
    int64_t nchunks = nv->val.int_val;
    unsigned char want[crypto_hash_sha256_BYTES];
    memcpy(want, sv->val.str_val.data, sizeof(want));
    bencode_free(d);

    if (total < 0 || (size_t)total > cap || nchunks < 0 || nchunks > KEYDIR_MAX_CHUNKS)
        return -1;

    size_t got = 0;
    for (int64_t i = 0; i < nchunks; i++) {
        char ck[160];
        if (nornd_keydir_gpg_chunk_key(id, (int)i, ck, sizeof(ck)) < 0) return -1; /* LCOV_EXCL_BR_LINE */
        if (got > (size_t)total) return -1; /* LCOV_EXCL_BR_LINE: total guards this */
        int cn = be->get(be->ctx, (const unsigned char *)ck, strlen(ck), out + got,
                         cap - got);
        if (cn < 0) return -1;
        got += (size_t)cn;
        if (got > (size_t)total) return -1; /* a chunk overran the declared length */
    }
    if (got != (size_t)total) return -1;

    unsigned char have[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(have, out, got);
    if (sodium_memcmp(have, want, sizeof(have)) != 0) return -1;
    return (int)got;
}
