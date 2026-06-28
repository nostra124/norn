/* Unit tests for the nornd fleet key directory (FEAT-031). 100% coverage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sodium.h>
#include "keydir.h"
#include "bencode.h"

/* ---- fake multi-key KV map backend ---- */
#define MAXK 256
static struct {
    unsigned char key[256];
    size_t klen;
    unsigned char val[8192];
    size_t vlen;
} MAP[MAXK];
static int MAPN;
static int g_put_calls;
static int g_put_fail_at = -1; /* fail the put with this call index */

static void map_reset(void) {
    MAPN = 0;
    g_put_calls = 0;
    g_put_fail_at = -1;
}
static int find_key(const unsigned char *k, size_t kl) {
    for (int i = 0; i < MAPN; i++)
        if (MAP[i].klen == kl && memcmp(MAP[i].key, k, kl) == 0) return i;
    return -1;
}
static int fput(void *c, const unsigned char *k, size_t kl, const unsigned char *v,
                size_t vl) {
    (void)c;
    if (g_put_calls++ == g_put_fail_at) return -1;
    int i = find_key(k, kl);
    if (i < 0) {
        if (MAPN >= MAXK) return -1;
        i = MAPN++;
        memcpy(MAP[i].key, k, kl);
        MAP[i].klen = kl;
    }
    memcpy(MAP[i].val, v, vl);
    MAP[i].vlen = vl;
    return 0;
}
static int fget(void *c, const unsigned char *k, size_t kl, unsigned char *o,
                size_t cap) {
    (void)c;
    int i = find_key(k, kl);
    if (i < 0) return -1;
    if (MAP[i].vlen > cap) return -1;
    memcpy(o, MAP[i].val, MAP[i].vlen);
    return (int)MAP[i].vlen;
}
static nornd_backend_t BE = {NULL, fput, NULL, fget, NULL, NULL, NULL, NULL};

static const unsigned char ID[NORND_PUBKEY] = {0xde, 0xad, 0xbe, 0xef};

/* Store a crafted bencode manifest at the gpg key. Frees `d`. */
static void put_manifest(bencode_value_t *d) {
    char mk[128];
    nornd_keydir_gpg_key(ID, mk, sizeof(mk));
    size_t ml = 0;
    char *e = bencode_encode(d, &ml);
    bencode_free(d);
    fput(NULL, (const unsigned char *)mk, strlen(mk), (const unsigned char *)e, ml);
    free(e);
}
static void put_chunk(int idx, const void *data, size_t n) {
    char ck[160];
    nornd_keydir_gpg_chunk_key(ID, idx, ck, sizeof(ck));
    fput(NULL, (const unsigned char *)ck, strlen(ck), data, n);
}

static void test_key_names(void) {
    char buf[128];
    assert(nornd_keydir_ssh_key(ID, buf, sizeof(buf)) > 0);
    assert(strncmp(buf, "peer/deadbeef", 13) == 0 &&
           strcmp(buf + strlen(buf) - 4, "/ssh") == 0);
    assert(nornd_keydir_gpg_key(ID, buf, sizeof(buf)) > 0);
    assert(nornd_keydir_gpg_chunk_key(ID, 7, buf, sizeof(buf)) > 0);
    assert(strcmp(buf + strlen(buf) - 6, "/gpg/7") == 0);
    /* too-small buffers hit the truncation branch */
    assert(nornd_keydir_ssh_key(ID, buf, 4) == -1);
    assert(nornd_keydir_gpg_key(ID, buf, 4) == -1);
    assert(nornd_keydir_gpg_chunk_key(ID, 7, buf, 4) == -1);
}

static void test_ssh(void) {
    map_reset();
    assert(nornd_keydir_put_ssh(&BE, ID, "ssh-ed25519 AAAAfoo comment") == 0);
    char out[256];
    int n = nornd_keydir_get_ssh(&BE, ID, out, sizeof(out));
    assert(n > 0 && strcmp(out, "ssh-ed25519 AAAAfoo comment") == 0);
    /* missing peer */
    unsigned char other[NORND_PUBKEY] = {1, 2, 3};
    nornd_backend_t be2 = BE;
    assert(nornd_keydir_get_ssh(&be2, other, out, sizeof(out)) == -1);
    /* put failure */
    map_reset();
    g_put_fail_at = 0;
    assert(nornd_keydir_put_ssh(&BE, ID, "x") == -1);
}

static void test_gpg_roundtrip(void) {
    /* single chunk */
    map_reset();
    const char *armor = "-----BEGIN PGP PUBLIC KEY BLOCK-----\nabc\n-----END-----\n";
    size_t alen = strlen(armor);
    assert(nornd_keydir_put_gpg(&BE, ID, (const unsigned char *)armor, alen) == 0);
    unsigned char out[200000];
    int n = nornd_keydir_get_gpg(&BE, ID, out, sizeof(out));
    assert(n == (int)alen && memcmp(out, armor, alen) == 0);

    /* multi-chunk: > NORND_KEYDIR_CHUNK forces several chunks */
    map_reset();
    size_t blen = NORND_KEYDIR_CHUNK * 2 + 17;
    unsigned char *big = malloc(blen);
    for (size_t i = 0; i < blen; i++) big[i] = (unsigned char)(i * 31 + 7);
    assert(nornd_keydir_put_gpg(&BE, ID, big, blen) == 0);
    n = nornd_keydir_get_gpg(&BE, ID, out, sizeof(out));
    assert(n == (int)blen && memcmp(out, big, blen) == 0);
    free(big);
}

static void test_gpg_put_errors(void) {
    map_reset();
    /* empty */
    assert(nornd_keydir_put_gpg(&BE, ID, (const unsigned char *)"x", 0) == -1);
    /* oversized */
    size_t huge = (size_t)NORND_KEYDIR_CHUNK * 64 + 1;
    unsigned char *z = calloc(1, huge);
    assert(nornd_keydir_put_gpg(&BE, ID, z, huge) == -1);
    free(z);
    /* chunk put fails (first put) */
    map_reset();
    g_put_fail_at = 0;
    assert(nornd_keydir_put_gpg(&BE, ID, (const unsigned char *)"hello", 5) == -1);
    /* manifest put fails (after the single chunk put) */
    map_reset();
    g_put_fail_at = 1;
    assert(nornd_keydir_put_gpg(&BE, ID, (const unsigned char *)"hello", 5) == -1);
}

static unsigned char SHA32[32];

static void test_gpg_get_errors(void) {
    char out[1024];
    unsigned char obuf[1024];

    /* manifest absent */
    map_reset();
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);

    /* manifest not bencode-decodable */
    map_reset();
    {
        char mk[128];
        nornd_keydir_gpg_key(ID, mk, sizeof(mk));
        fput(NULL, (const unsigned char *)mk, strlen(mk),
             (const unsigned char *)"not-bencode!!", 13);
    }
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);

    /* field-validation: omit/typo each field */
    bencode_value_t *d;
    /* missing len */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "n", bencode_int_new(1));
    bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 32));
    put_manifest(d);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);
    /* len wrong type */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "len", bencode_string_new("x", 1));
    bencode_dict_add(d, "n", bencode_int_new(1));
    bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 32));
    put_manifest(d);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);
    /* missing n */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "len", bencode_int_new(5));
    bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 32));
    put_manifest(d);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);
    /* n wrong type */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "len", bencode_int_new(5));
    bencode_dict_add(d, "n", bencode_string_new("x", 1));
    bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 32));
    put_manifest(d);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);
    /* missing sha */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "len", bencode_int_new(5));
    bencode_dict_add(d, "n", bencode_int_new(1));
    put_manifest(d);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);
    /* sha wrong type */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "len", bencode_int_new(5));
    bencode_dict_add(d, "n", bencode_int_new(1));
    bencode_dict_add(d, "sha256", bencode_int_new(0));
    put_manifest(d);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);
    /* sha wrong length */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "len", bencode_int_new(5));
    bencode_dict_add(d, "n", bencode_int_new(1));
    bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 5));
    put_manifest(d);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);

    /* numeric guards: total<0, total>cap, nchunks<0, nchunks>MAX */
    int64_t bad_total[] = {-1, 100000};
    for (int j = 0; j < 2; j++) {
        map_reset();
        d = bencode_dict_new();
        bencode_dict_add(d, "len", bencode_int_new(bad_total[j]));
        bencode_dict_add(d, "n", bencode_int_new(1));
        bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 32));
        put_manifest(d);
        assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);
    }
    int64_t bad_n[] = {-1, 65};
    for (int j = 0; j < 2; j++) {
        map_reset();
        d = bencode_dict_new();
        bencode_dict_add(d, "len", bencode_int_new(5));
        bencode_dict_add(d, "n", bencode_int_new(bad_n[j]));
        bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 32));
        put_manifest(d);
        assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);
    }

    /* chunk missing (manifest says 1 chunk, none stored) */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "len", bencode_int_new(5));
    bencode_dict_add(d, "n", bencode_int_new(1));
    bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 32));
    put_manifest(d);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);

    /* chunk overruns declared length (len=2 but chunk has 5 bytes) */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "len", bencode_int_new(2));
    bencode_dict_add(d, "n", bencode_int_new(1));
    bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 32));
    put_manifest(d);
    put_chunk(0, "hello", 5);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);

    /* short total: len=10 but only 5 bytes available → got != total */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "len", bencode_int_new(10));
    bencode_dict_add(d, "n", bencode_int_new(1));
    bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 32));
    put_manifest(d);
    put_chunk(0, "hello", 5);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);

    /* sha mismatch: len matches but hash is wrong */
    map_reset();
    d = bencode_dict_new();
    bencode_dict_add(d, "len", bencode_int_new(5));
    bencode_dict_add(d, "n", bencode_int_new(1));
    bencode_dict_add(d, "sha256", bencode_string_new((char *)SHA32, 32)); /* zeros */
    put_manifest(d);
    put_chunk(0, "hello", 5);
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, sizeof(obuf)) == -1);

    /* get with cap too small for the value → total>cap */
    map_reset();
    {
        const char *a = "hello";
        nornd_keydir_put_gpg(&BE, ID, (const unsigned char *)a, 5);
    }
    assert(nornd_keydir_get_gpg(&BE, ID, obuf, 3) == -1);
    (void)out;
}

int main(void) {
    if (sodium_init() < 0) return 1;
    test_key_names();
    test_ssh();
    test_gpg_roundtrip();
    test_gpg_put_errors();
    test_gpg_get_errors();
    printf("all nornd keydir tests passed\n");
    return 0;
}
