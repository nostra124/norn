/* SPDX-License-Identifier: MIT */
/**
 * @file identity.c
 * @brief SSH-key node identity for nornd (FEAT-028). See identity.h.
 *
 * Parses unencrypted OpenSSH ed25519 private keys and `ssh-ed25519` public-key
 * lines into libsodium keypairs. The OpenSSH v1 private-key container is:
 *
 *   "openssh-key-v1\0"
 *   string  ciphername          ("none" — encrypted keys unsupported)
 *   string  kdfname             ("none")
 *   string  kdfoptions          ("")
 *   uint32  number of keys      (1)
 *   string  public key blob
 *   string  private section (one key, since cipher is "none"):
 *       uint32 checkint, uint32 checkint   (must match)
 *       string keytype          ("ssh-ed25519")
 *       string pub[32]
 *       string priv[64]         (== seed‖pub == libsodium secret key)
 *       string comment
 *       padding 1,2,3,…
 *
 * All multi-byte integers are big-endian; strings are uint32 length + bytes.
 */

#include "identity.h"

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t cap, const char *msg) {
    if (err && cap) {
        size_t n = strlen(msg);
        if (n >= cap) n = cap - 1;
        memcpy(err, msg, n);
        err[n] = '\0';
    }
}

/* Bounds-checked big-endian reader over a fixed buffer. */
typedef struct {
    const unsigned char *p;
    size_t len;
    size_t pos;
} rd_t;

static int rd_u32(rd_t *r, uint32_t *out) {
    if (r->pos + 4 > r->len) return -1;
    *out = ((uint32_t)r->p[r->pos] << 24) | ((uint32_t)r->p[r->pos + 1] << 16) |
           ((uint32_t)r->p[r->pos + 2] << 8) | (uint32_t)r->p[r->pos + 3];
    r->pos += 4;
    return 0;
}

/* Read a uint32-length-prefixed string; yields a pointer into the buffer. */
static int rd_str(rd_t *r, const unsigned char **data, uint32_t *slen) {
    uint32_t n;
    if (rd_u32(r, &n) != 0) return -1;
    if ((size_t)n > r->len - r->pos) return -1;
    *data = r->p + r->pos;
    *slen = n;
    r->pos += n;
    return 0;
}

/* Compare a parsed string against a NUL-terminated literal. */
static int str_eq(const unsigned char *d, uint32_t n, const char *lit) {
    return n == strlen(lit) && memcmp(d, lit, n) == 0;
}

static const char PEM_BEGIN[] = "-----BEGIN OPENSSH PRIVATE KEY-----";
static const char PEM_END[] = "-----END OPENSSH PRIVATE KEY-----";
static const char OPENSSH_MAGIC[] = "openssh-key-v1"; /* + trailing NUL */

int nornd_identity_parse_openssh(const unsigned char *pem, size_t pemlen,
                                 keypair_t *out, char *err, size_t errcap) {
    if (!pem || !out) {
        set_err(err, errcap, "null argument");
        return -1;
    }
    /* Locate the base64 body between the BEGIN and END armor lines. */
    const char *text = (const char *)pem;
    const char *begin = NULL;
    for (size_t i = 0; i + sizeof(PEM_BEGIN) - 1 <= pemlen; i++) {
        if (memcmp(text + i, PEM_BEGIN, sizeof(PEM_BEGIN) - 1) == 0) {
            begin = text + i + sizeof(PEM_BEGIN) - 1;
            break;
        }
    }
    if (!begin) {
        set_err(err, errcap, "missing BEGIN marker");
        return -1;
    }
    const char *tail_end = text + pemlen;
    const char *end = NULL;
    for (const char *q = begin; q + sizeof(PEM_END) - 1 <= tail_end; q++) {
        if (memcmp(q, PEM_END, sizeof(PEM_END) - 1) == 0) {
            end = q;
            break;
        }
    }
    if (!end) {
        set_err(err, errcap, "missing END marker");
        return -1;
    }

    /* Base64-decode the armored body (ignore embedded whitespace/newlines). */
    size_t b64len = (size_t)(end - begin);
    unsigned char *bin = malloc(b64len > 0 ? b64len : 1);
    if (!bin) { /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
        set_err(err, errcap, "out of memory"); /* LCOV_EXCL_LINE */
        return -1;                             /* LCOV_EXCL_LINE */
    }
    size_t binlen = 0;
    if (sodium_base642bin(bin, b64len, begin, b64len, " \r\n\t", &binlen, NULL,
                          sodium_base64_VARIANT_ORIGINAL) != 0) {
        free(bin);
        set_err(err, errcap, "base64 decode failed");
        return -1;
    }

    rd_t r = {bin, binlen, 0};
    int rc = -1;

    /* magic "openssh-key-v1\0" */
    size_t maglen = sizeof(OPENSSH_MAGIC); /* includes NUL */
    if (binlen < maglen || memcmp(bin, OPENSSH_MAGIC, maglen) != 0) {
        set_err(err, errcap, "bad magic");
        goto done;
    }
    r.pos = maglen;

    const unsigned char *s;
    uint32_t sl;
    /* ciphername must be "none" (we don't decrypt). */
    if (rd_str(&r, &s, &sl) != 0) {
        set_err(err, errcap, "truncated ciphername");
        goto done;
    }
    if (!str_eq(s, sl, "none")) {
        set_err(err, errcap, "encrypted key unsupported");
        goto done;
    }
    /* kdfname must be "none". */
    if (rd_str(&r, &s, &sl) != 0) {
        set_err(err, errcap, "truncated kdfname");
        goto done;
    }
    if (!str_eq(s, sl, "none")) {
        set_err(err, errcap, "encrypted key unsupported");
        goto done;
    }
    /* kdfoptions (skip). */
    if (rd_str(&r, &s, &sl) != 0) {
        set_err(err, errcap, "truncated kdfoptions");
        goto done;
    }
    /* number of keys must be 1. */
    uint32_t nkeys;
    if (rd_u32(&r, &nkeys) != 0) {
        set_err(err, errcap, "truncated key count");
        goto done;
    }
    if (nkeys != 1) {
        set_err(err, errcap, "unsupported key count");
        goto done;
    }
    /* public key blob (skip; the private section repeats it). */
    if (rd_str(&r, &s, &sl) != 0) {
        set_err(err, errcap, "truncated public blob");
        goto done;
    }
    /* private section. */
    const unsigned char *priv;
    uint32_t privlen;
    if (rd_str(&r, &priv, &privlen) != 0) {
        set_err(err, errcap, "truncated private section");
        goto done;
    }

    rd_t pr = {priv, privlen, 0};
    uint32_t c1, c2;
    if (rd_u32(&pr, &c1) != 0 || rd_u32(&pr, &c2) != 0) {
        set_err(err, errcap, "truncated checkint");
        goto done;
    }
    if (c1 != c2) {
        set_err(err, errcap, "checkint mismatch");
        goto done;
    }
    /* keytype "ssh-ed25519". */
    if (rd_str(&pr, &s, &sl) != 0) {
        set_err(err, errcap, "truncated keytype");
        goto done;
    }
    if (!str_eq(s, sl, "ssh-ed25519")) {
        set_err(err, errcap, "not an ed25519 key");
        goto done;
    }
    /* public key (32 bytes). */
    const unsigned char *pub;
    uint32_t publen;
    if (rd_str(&pr, &pub, &publen) != 0 || publen != CRYPTO_PUBLICKEYBYTES) {
        set_err(err, errcap, "bad public key length");
        goto done;
    }
    /* private key (64 bytes = seed‖pub == libsodium secret key). */
    const unsigned char *sk;
    uint32_t sklen;
    if (rd_str(&pr, &sk, &sklen) != 0 || sklen != CRYPTO_SECRETKEYBYTES) {
        set_err(err, errcap, "bad private key length");
        goto done;
    }

    memcpy(out->public_key, pub, CRYPTO_PUBLICKEYBYTES);
    memcpy(out->secret_key, sk, CRYPTO_SECRETKEYBYTES);
    rc = 0;

done:
    sodium_memzero(bin, binlen);
    free(bin);
    return rc;
}

int nornd_identity_parse_pubkey_line(const char *line, size_t len,
                                     unsigned char pub[32]) {
    if (!line || !pub) return -1;
    /* Skip leading whitespace. */
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    static const char TYPE[] = "ssh-ed25519";
    size_t tn = sizeof(TYPE) - 1;
    if (len - i < tn || memcmp(line + i, TYPE, tn) != 0) return -1;
    i += tn;
    /* Exactly one space/tab separates type and the base64 blob. */
    if (i >= len || (line[i] != ' ' && line[i] != '\t')) return -1;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    /* The base64 field runs until the next whitespace (or end). */
    size_t b = i;
    while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != '\n' &&
           line[i] != '\r')
        i++;
    size_t b64len = i - b;
    if (b64len == 0) return -1;

    unsigned char blob[64];
    size_t blen = 0;
    if (sodium_base642bin(blob, sizeof(blob), line + b, b64len, "", &blen, NULL,
                          sodium_base64_VARIANT_ORIGINAL) != 0)
        return -1;

    /* Blob: string "ssh-ed25519" + string pub[32]. */
    rd_t r = {blob, blen, 0};
    const unsigned char *s;
    uint32_t sl;
    if (rd_str(&r, &s, &sl) != 0 || !str_eq(s, sl, "ssh-ed25519")) return -1;
    const unsigned char *pk;
    uint32_t pl;
    if (rd_str(&r, &pk, &pl) != 0 || pl != CRYPTO_PUBLICKEYBYTES) return -1;
    memcpy(pub, pk, CRYPTO_PUBLICKEYBYTES);
    return 0;
}

int nornd_identity_load_file(const char *path, keypair_t *out, char *err,
                             size_t errcap) {
    if (!path || !out) {
        set_err(err, errcap, "null argument");
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_err(err, errcap, "cannot open key file");
        return -1;
    }
    /* Private keys are tiny; cap the read generously. */
    unsigned char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf), f);
    int short_read = feof(f);
    fclose(f);
    if (!short_read) { /* file larger than buffer => not a normal key */
        set_err(err, errcap, "key file too large");
        return -1;
    }
    return nornd_identity_parse_openssh(buf, n, out, err, errcap);
}
