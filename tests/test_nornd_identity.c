/* Unit tests for nornd SSH-key identity (FEAT-028). Pure parsers → 100% cov. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sodium.h>
#include "identity.h"

/* ---- a real ssh-keygen ed25519 key (unencrypted) and its known scalars ---- */
static const char REAL_PEM[] =
    "-----BEGIN OPENSSH PRIVATE KEY-----\n"
    "b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtzc2gtZW\n"
    "QyNTUxOQAAACBRyezVzn5hvdKved9fz0FIwAa1YQf+HQdCmbEZoh5UxQAAAJBv45F5b+OR\n"
    "eQAAAAtzc2gtZWQyNTUxOQAAACBRyezVzn5hvdKved9fz0FIwAa1YQf+HQdCmbEZoh5UxQ\n"
    "AAAEDT3T8Oo82Koy9XFkFYSk96DO7BlJ+IoKpxTxwhGrhlIVHJ7NXOfmG90q9531/PQUjA\n"
    "BrVhB/4dB0KZsRmiHlTFAAAACW5vcm4tdGVzdAECAwQ=\n"
    "-----END OPENSSH PRIVATE KEY-----\n";
static const char REAL_PUBLINE[] =
    "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFHJ7NXOfmG90q9531/PQUjABrVhB/4dB0KZsRm"
    "iHlTF norn-test\n";
/* 51c9ecd5ce7e61bdd2af79df5fcf4148c006b56107fe1d074299b119a21e54c5 */
static const unsigned char REAL_PUB[32] = {
    0x51, 0xc9, 0xec, 0xd5, 0xce, 0x7e, 0x61, 0xbd, 0xd2, 0xaf, 0x79,
    0xdf, 0x5f, 0xcf, 0x41, 0x48, 0xc0, 0x06, 0xb5, 0x61, 0x07, 0xfe,
    0x1d, 0x07, 0x42, 0x99, 0xb1, 0x19, 0xa2, 0x1e, 0x54, 0xc5};
/* d3dd3f0ea3cd8aa32f571641584a4f7a0ceec1949f88a0aa714f1c211ab86521 */
static const unsigned char REAL_SEED[32] = {
    0xd3, 0xdd, 0x3f, 0x0e, 0xa3, 0xcd, 0x8a, 0xa3, 0x2f, 0x57, 0x16,
    0x41, 0x58, 0x4a, 0x4f, 0x7a, 0x0c, 0xee, 0xc1, 0x94, 0x9f, 0x88,
    0xa0, 0xaa, 0x71, 0x4f, 0x1c, 0x21, 0x1a, 0xb8, 0x65, 0x21};

/* ---- synthetic openssh-key-v1 blob builder (for exhaustive branch cov) ---- */
typedef struct {
    unsigned char b[8192];
    size_t n;
} buf_t;

static void put_raw(buf_t *b, const void *d, size_t n) {
    memcpy(b->b + b->n, d, n);
    b->n += n;
}
static void put_u32(buf_t *b, uint32_t v) {
    unsigned char t[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16),
                          (unsigned char)(v >> 8), (unsigned char)v};
    put_raw(b, t, 4);
}
static void put_str(buf_t *b, const void *d, uint32_t n) {
    put_u32(b, n);
    put_raw(b, d, n);
}
static void put_cstr(buf_t *b, const char *s) { put_str(b, s, (uint32_t)strlen(s)); }
static void put_magic(buf_t *b) { put_raw(b, "openssh-key-v1", 15); /* +NUL */ }

/* Build a private section (checkints, keytype, pub, sk). */
static void build_priv(buf_t *p, uint32_t c1, uint32_t c2, const char *keytype,
                       const unsigned char *pub, uint32_t publen,
                       const unsigned char *sk, uint32_t sklen) {
    p->n = 0;
    put_u32(p, c1);
    put_u32(p, c2);
    put_cstr(p, keytype);
    put_str(p, pub, publen);
    put_str(p, sk, sklen);
}

/* Wrap a binary blob as armored PEM into out (NUL-terminated). */
static void to_pem(const unsigned char *blob, size_t n, char *out, size_t cap) {
    char b64[16384];
    sodium_bin2base64(b64, sizeof(b64), blob, n, sodium_base64_VARIANT_ORIGINAL);
    snprintf(out, cap,
             "-----BEGIN OPENSSH PRIVATE KEY-----\n%s\n"
             "-----END OPENSSH PRIVATE KEY-----\n",
             b64);
}

/* Decode a synthetic blob; returns parse rc. */
static int parse_blob(const unsigned char *blob, size_t n, keypair_t *kp) {
    char pem[20000];
    to_pem(blob, n, pem, sizeof(pem));
    return nornd_identity_parse_openssh((const unsigned char *)pem, strlen(pem),
                                        kp, NULL, 0);
}

/* A full, valid synthetic key built from arbitrary pub/sk. */
static void build_valid(buf_t *o, const unsigned char *pub,
                        const unsigned char *sk) {
    buf_t priv;
    build_priv(&priv, 0x11223344u, 0x11223344u, "ssh-ed25519", pub, 32, sk, 64);
    o->n = 0;
    put_magic(o);
    put_cstr(o, "none");
    put_cstr(o, "none");
    put_cstr(o, ""); /* kdfoptions */
    put_u32(o, 1);   /* nkeys */
    put_cstr(o, "pubblob-ignored");
    put_str(o, priv.b, (uint32_t)priv.n);
}

static void test_real_private(void) {
    keypair_t kp;
    char err[64];
    assert(nornd_identity_parse_openssh((const unsigned char *)REAL_PEM,
                                        strlen(REAL_PEM), &kp, err,
                                        sizeof(err)) == 0);
    assert(memcmp(kp.public_key, REAL_PUB, 32) == 0);
    assert(memcmp(kp.secret_key, REAL_SEED, 32) == 0);      /* seed half */
    assert(memcmp(kp.secret_key + 32, REAL_PUB, 32) == 0);  /* pub half  */
}

static void test_real_publine(void) {
    unsigned char pub[32];
    assert(nornd_identity_parse_pubkey_line(REAL_PUBLINE, strlen(REAL_PUBLINE),
                                            pub) == 0);
    assert(memcmp(pub, REAL_PUB, 32) == 0);
}

static void test_synth_valid(void) {
    unsigned char pub[32], sk[64];
    memset(pub, 0x5a, sizeof(pub));
    memset(sk, 0xa5, sizeof(sk));
    buf_t o;
    build_valid(&o, pub, sk);
    keypair_t kp;
    assert(parse_blob(o.b, o.n, &kp) == 0);
    assert(memcmp(kp.public_key, pub, 32) == 0);
    assert(memcmp(kp.secret_key, sk, 64) == 0);
}

static void test_armor_errors(void) {
    keypair_t kp;
    char err[64];
    /* null args */
    assert(nornd_identity_parse_openssh(NULL, 0, &kp, err, sizeof(err)) == -1);
    assert(nornd_identity_parse_openssh((const unsigned char *)REAL_PEM,
                                        strlen(REAL_PEM), NULL, err,
                                        sizeof(err)) == -1);
    /* missing BEGIN */
    const char *nob = "no markers here";
    assert(nornd_identity_parse_openssh((const unsigned char *)nob, strlen(nob),
                                        &kp, err, sizeof(err)) == -1);
    /* BEGIN but no END */
    const char *noe = "-----BEGIN OPENSSH PRIVATE KEY-----\nAAAA\n";
    assert(nornd_identity_parse_openssh((const unsigned char *)noe, strlen(noe),
                                        &kp, err, sizeof(err)) == -1);
    /* empty body (b64len == 0) → bad magic via binlen < maglen */
    const char *empty =
        "-----BEGIN OPENSSH PRIVATE KEY-----\n"
        "-----END OPENSSH PRIVATE KEY-----\n";
    assert(nornd_identity_parse_openssh((const unsigned char *)empty,
                                        strlen(empty), &kp, err,
                                        sizeof(err)) == -1);
    /* invalid base64 between markers (err == NULL exercises set_err skip) */
    const char *badb64 =
        "-----BEGIN OPENSSH PRIVATE KEY-----\n!!!!\n"
        "-----END OPENSSH PRIVATE KEY-----\n";
    assert(nornd_identity_parse_openssh((const unsigned char *)badb64,
                                        strlen(badb64), &kp, NULL, 0) == -1);
    /* wrong magic, long enough body */
    buf_t o;
    o.n = 0;
    put_raw(&o, "not-openssh-keyXX", 17);
    assert(parse_blob(o.b, o.n, &kp) == -1);
    /* tiny errcap forces set_err truncation */
    char tiny[4];
    assert(nornd_identity_parse_openssh((const unsigned char *)nob, strlen(nob),
                                        &kp, tiny, sizeof(tiny)) == -1);
    assert(strlen(tiny) == 3);
    /* err buffer present but cap == 0 (set_err must not write) */
    char zcap[8];
    zcap[0] = 'Z';
    assert(nornd_identity_parse_openssh((const unsigned char *)nob, strlen(nob),
                                        &kp, zcap, 0) == -1);
    assert(zcap[0] == 'Z'); /* untouched */
    /* BEGIN marker not at offset 0 (scan skips non-matching bytes first) */
    char pre[sizeof(REAL_PEM) + 40];
    snprintf(pre, sizeof(pre), "junk-prefix-padding-here\n%s", REAL_PEM);
    assert(nornd_identity_parse_openssh((const unsigned char *)pre, strlen(pre),
                                        &kp, err, sizeof(err)) == 0);
    assert(memcmp(kp.public_key, REAL_PUB, 32) == 0);
    /* BEGIN immediately followed by END → empty body (b64len == 0) */
    const char *adj =
        "-----BEGIN OPENSSH PRIVATE KEY----------END OPENSSH PRIVATE KEY-----";
    assert(nornd_identity_parse_openssh((const unsigned char *)adj, strlen(adj),
                                        &kp, err, sizeof(err)) == -1);
}

static void test_truncations(void) {
    keypair_t kp;
    buf_t o;
    /* magic only → truncated ciphername (rd_u32 fail) */
    o.n = 0;
    put_magic(&o);
    assert(parse_blob(o.b, o.n, &kp) == -1);
    /* + ciphername → truncated kdfname */
    put_cstr(&o, "none");
    assert(parse_blob(o.b, o.n, &kp) == -1);
    /* + kdfname → truncated kdfoptions */
    put_cstr(&o, "none");
    assert(parse_blob(o.b, o.n, &kp) == -1);
    /* + kdfoptions → truncated key count */
    put_cstr(&o, "");
    assert(parse_blob(o.b, o.n, &kp) == -1);
    /* + nkeys, then a pub-blob length claiming bytes that aren't there
       → truncated public blob (rd_str length-fail branch) */
    put_u32(&o, 1);
    put_u32(&o, 9999);
    assert(parse_blob(o.b, o.n, &kp) == -1);
    /* rebuild up to a valid pub blob, then a priv length with no data
       → truncated private section */
    o.n = 0;
    put_magic(&o);
    put_cstr(&o, "none");
    put_cstr(&o, "none");
    put_cstr(&o, "");
    put_u32(&o, 1);
    put_cstr(&o, "pub");
    put_u32(&o, 9999); /* claims a huge private section */
    assert(parse_blob(o.b, o.n, &kp) == -1);
}

/* Build an outer blob wrapping a given private section verbatim. */
static void wrap_priv(buf_t *o, const buf_t *priv) {
    o->n = 0;
    put_magic(o);
    put_cstr(o, "none");
    put_cstr(o, "none");
    put_cstr(o, "");
    put_u32(o, 1);
    put_cstr(o, "pubblob");
    put_str(o, priv->b, (uint32_t)priv->n);
}

static void test_cipher_kdf_nkeys(void) {
    keypair_t kp;
    unsigned char pub[32] = {0}, sk[64] = {0};
    buf_t priv;
    build_priv(&priv, 7, 7, "ssh-ed25519", pub, 32, sk, 64);

    /* encrypted: ciphername != "none" (different length → str_eq false) */
    buf_t o;
    o.n = 0;
    put_magic(&o);
    put_cstr(&o, "aes256-ctr");
    put_cstr(&o, "none");
    put_cstr(&o, "");
    put_u32(&o, 1);
    put_cstr(&o, "pub");
    put_str(&o, priv.b, (uint32_t)priv.n);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* kdfname != "none" */
    o.n = 0;
    put_magic(&o);
    put_cstr(&o, "none");
    put_cstr(&o, "bcrypt");
    put_cstr(&o, "");
    put_u32(&o, 1);
    put_cstr(&o, "pub");
    put_str(&o, priv.b, (uint32_t)priv.n);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* nkeys != 1 */
    o.n = 0;
    put_magic(&o);
    put_cstr(&o, "none");
    put_cstr(&o, "none");
    put_cstr(&o, "");
    put_u32(&o, 2);
    put_cstr(&o, "pub");
    put_str(&o, priv.b, (uint32_t)priv.n);
    assert(parse_blob(o.b, o.n, &kp) == -1);
}

static void test_priv_section_errors(void) {
    keypair_t kp;
    unsigned char pub[32] = {0}, sk[64] = {0};
    buf_t priv, o;

    /* private section shorter than one u32 → first checkint read fails */
    priv.n = 0;
    put_raw(&priv, "ab", 2);
    wrap_priv(&o, &priv);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* truncated checkint: priv shorter than 8 bytes */
    priv.n = 0;
    put_u32(&priv, 1);
    wrap_priv(&o, &priv);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* checkint mismatch */
    build_priv(&priv, 1, 2, "ssh-ed25519", pub, 32, sk, 64);
    wrap_priv(&o, &priv);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* truncated keytype: only checkints present */
    priv.n = 0;
    put_u32(&priv, 5);
    put_u32(&priv, 5);
    wrap_priv(&o, &priv);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* not ed25519 (different length) */
    build_priv(&priv, 5, 5, "ssh-rsa", pub, 32, sk, 64);
    wrap_priv(&o, &priv);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* keytype same length as "ssh-ed25519" but different bytes (str_eq memcmp) */
    build_priv(&priv, 5, 5, "ssh-ed25518", pub, 32, sk, 64);
    wrap_priv(&o, &priv);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* public key wrong length (present but != 32) */
    build_priv(&priv, 5, 5, "ssh-ed25519", pub, 16, sk, 64);
    wrap_priv(&o, &priv);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* public key rd_str fail: keytype ok then a u32 claiming too much */
    priv.n = 0;
    put_u32(&priv, 5);
    put_u32(&priv, 5);
    put_cstr(&priv, "ssh-ed25519");
    put_u32(&priv, 9999);
    wrap_priv(&o, &priv);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* private key wrong length (present but != 64) */
    build_priv(&priv, 5, 5, "ssh-ed25519", pub, 32, sk, 32);
    wrap_priv(&o, &priv);
    assert(parse_blob(o.b, o.n, &kp) == -1);

    /* private key rd_str fail: pub ok then a u32 claiming too much */
    priv.n = 0;
    put_u32(&priv, 5);
    put_u32(&priv, 5);
    put_cstr(&priv, "ssh-ed25519");
    put_str(&priv, pub, 32);
    put_u32(&priv, 9999);
    wrap_priv(&o, &priv);
    assert(parse_blob(o.b, o.n, &kp) == -1);
}

/* ---- public-key line parser ---- */
static void test_publine_errors(void) {
    unsigned char pub[32];
    /* null args */
    assert(nornd_identity_parse_pubkey_line(NULL, 0, pub) == -1);
    assert(nornd_identity_parse_pubkey_line("x", 1, NULL) == -1);
    /* too short for the type token */
    assert(nornd_identity_parse_pubkey_line("ssh", 3, pub) == -1);
    /* wrong type, same length region */
    assert(nornd_identity_parse_pubkey_line("ssh-rsaXXXXXX", 13, pub) == -1);
    /* type present but no separator (line ends) */
    assert(nornd_identity_parse_pubkey_line("ssh-ed25519", 11, pub) == -1);
    /* type present but next char is not a separator */
    assert(nornd_identity_parse_pubkey_line("ssh-ed25519X", 12, pub) == -1);
    /* leading whitespace then type, but only spaces after (b64len == 0) */
    assert(nornd_identity_parse_pubkey_line("  ssh-ed25519   ", 16, pub) == -1);
    /* invalid base64 */
    assert(nornd_identity_parse_pubkey_line("ssh-ed25519 !!!!", 16, pub) == -1);
}

/* Encode a pubkey blob (type + pub) to base64 and wrap as a line. */
static void publine_from_blob(const buf_t *blob, char *out, size_t cap) {
    char b64[1024];
    sodium_bin2base64(b64, sizeof(b64), blob->b, blob->n,
                      sodium_base64_VARIANT_ORIGINAL);
    snprintf(out, cap, "ssh-ed25519 %s comment\n", b64);
}

static void test_publine_blob_errors(void) {
    unsigned char pub[32];
    char line[1024];
    buf_t blob;
    /* inner type mismatch */
    blob.n = 0;
    put_cstr(&blob, "ssh-rsa");
    put_str(&blob, pub, 32);
    publine_from_blob(&blob, line, sizeof(line));
    assert(nornd_identity_parse_pubkey_line(line, strlen(line), pub) == -1);
    /* inner type ok but rd_str for pub fails (no pub field) */
    blob.n = 0;
    put_cstr(&blob, "ssh-ed25519");
    publine_from_blob(&blob, line, sizeof(line));
    assert(nornd_identity_parse_pubkey_line(line, strlen(line), pub) == -1);
    /* inner pub wrong length */
    blob.n = 0;
    put_cstr(&blob, "ssh-ed25519");
    put_str(&blob, pub, 16);
    publine_from_blob(&blob, line, sizeof(line));
    assert(nornd_identity_parse_pubkey_line(line, strlen(line), pub) == -1);
    /* blob too short for even a length prefix → first rd_str fails */
    blob.n = 0;
    put_raw(&blob, "ab", 2);
    publine_from_blob(&blob, line, sizeof(line));
    assert(nornd_identity_parse_pubkey_line(line, strlen(line), pub) == -1);
}

/* Whitespace handling: tabs as separators, varied line terminators. */
static const char REAL_B64[] =
    "AAAAC3NzaC1lZDI1NTE5AAAAIFHJ7NXOfmG90q9531/PQUjABrVhB/4dB0KZsRmiHlTF";

static void test_publine_whitespace(void) {
    unsigned char pub[32];
    char line[1024];
    /* leading tab, tab separator, tab before comment */
    snprintf(line, sizeof(line), "\tssh-ed25519\t%s\tcomment\n", REAL_B64);
    assert(nornd_identity_parse_pubkey_line(line, strlen(line), pub) == 0);
    assert(memcmp(pub, REAL_PUB, 32) == 0);
    /* base64 field ends at '\n' (no comment) */
    snprintf(line, sizeof(line), "ssh-ed25519 %s\n", REAL_B64);
    assert(nornd_identity_parse_pubkey_line(line, strlen(line), pub) == 0);
    assert(memcmp(pub, REAL_PUB, 32) == 0);
    /* base64 field ends at '\r' */
    snprintf(line, sizeof(line), "ssh-ed25519 %s\r\n", REAL_B64);
    assert(nornd_identity_parse_pubkey_line(line, strlen(line), pub) == 0);
    assert(memcmp(pub, REAL_PUB, 32) == 0);
    /* line is only whitespace → leading-skip reaches end, type check fails */
    assert(nornd_identity_parse_pubkey_line("    ", 4, pub) == -1);
}

/* ---- file loader ---- */
static void test_load_file(void) {
    keypair_t kp;
    char err[64];
    /* null args */
    assert(nornd_identity_load_file(NULL, &kp, err, sizeof(err)) == -1);
    assert(nornd_identity_load_file("x", NULL, err, sizeof(err)) == -1);
    /* nonexistent path */
    assert(nornd_identity_load_file("/no/such/key/file", &kp, err,
                                    sizeof(err)) == -1);

    const char *dir = getenv("TMPDIR");
    if (!dir) dir = "/tmp";
    char path[512];

    /* valid key file */
    snprintf(path, sizeof(path), "%s/nornd_id_%d.key", dir, (int)getpid());
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite(REAL_PEM, 1, strlen(REAL_PEM), f);
    fclose(f);
    assert(nornd_identity_load_file(path, &kp, err, sizeof(err)) == 0);
    assert(memcmp(kp.public_key, REAL_PUB, 32) == 0);
    remove(path);

    /* oversized file (> internal cap) → "too large" */
    snprintf(path, sizeof(path), "%s/nornd_big_%d.key", dir, (int)getpid());
    f = fopen(path, "wb");
    assert(f);
    char chunk[1024];
    memset(chunk, 'A', sizeof(chunk));
    for (int i = 0; i < 20; i++) fwrite(chunk, 1, sizeof(chunk), f); /* 20 KiB */
    fclose(f);
    assert(nornd_identity_load_file(path, &kp, err, sizeof(err)) == -1);
    remove(path);
}

int main(void) {
    if (sodium_init() < 0) return 1;
    test_real_private();
    test_real_publine();
    test_synth_valid();
    test_armor_errors();
    test_truncations();
    test_cipher_kdf_nkeys();
    test_priv_section_errors();
    test_publine_errors();
    test_publine_blob_errors();
    test_publine_whitespace();
    test_load_file();
    printf("all nornd identity tests passed\n");
    return 0;
}
