/* SPDX-License-Identifier: MIT */
#include "recstore.h"
#include "bep44.h"
#include "crypto.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sodium.h>
#include <unistd.h>

static void test_init(void) {
    int ret = recstore_init(NULL);
    assert(ret == -1 || ret >= 0);
    
    printf("  test_init: OK\n");
}

static void test_accept(void) {
    recstore_init("/tmp/norn_test_recstore");
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "test record";
    unsigned char sig[64];
    
    int ret = recstore_accept(pk, 1, value, sizeof(value) - 1, sig);
    assert(ret == 0 || ret == 1);
    
    printf("  test_accept: OK\n");
}

static void test_accept_null(void) {
    unsigned char pk[32] = {0};
    unsigned char value[] = "test";
    unsigned char sig[64] = {0};
    
    int ret = recstore_accept(NULL, 1, value, 4, sig);
    assert(ret == 0);
    
    ret = recstore_accept(pk, 1, NULL, 4, sig);
    assert(ret == 0);
    
    ret = recstore_accept(pk, 1, value, 0, sig);
    assert(ret == 0);
    
    ret = recstore_accept(pk, 1, value, 4, NULL);
    assert(ret == 0);
    
    printf("  test_accept_null: OK\n");
}

static void test_accept_signed(void) {
    const char *path = "/tmp/norn_test_recstore_signed";
    unlink(path);
    
    recstore_init(path);
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "signed test record";
    unsigned char target[20];
    bep44_target(pk, target);
    
    unsigned char buf[256];
    int buflen = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    assert(buflen > 0);
    
    unsigned char sig[64];
    int ret = bf_sign(sig, buf, (size_t)buflen, sk);
    assert(ret == 0);
    
    ret = recstore_accept(pk, 1, value, sizeof(value) - 1, sig);
    assert(ret == 1);
    
    rec_t out;
    ret = recstore_get(target, &out);
    assert(ret == 1);
    assert(out.seq == 1);
    assert(out.vlen == sizeof(value) - 1);
    assert(memcmp(out.v, value, out.vlen) == 0);
    
    unlink(path);
    printf("  test_accept_signed: OK\n");
}

static void test_accept_stale(void) {
    const char *path = "/tmp/norn_test_recstore_stale";
    unlink(path);
    
    recstore_init(path);
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "test value";
    unsigned char buf[256];
    int buflen = bep44_signbuf(10, value, sizeof(value) - 1, buf, sizeof(buf));
    
    unsigned char sig[64];
    bf_sign(sig, buf, (size_t)buflen, sk);
    
    int ret = recstore_accept(pk, 10, value, sizeof(value) - 1, sig);
    assert(ret == 1);
    
    buflen = bep44_signbuf(5, value, sizeof(value) - 1, buf, sizeof(buf));
    bf_sign(sig, buf, (size_t)buflen, sk);
    ret = recstore_accept(pk, 5, value, sizeof(value) - 1, sig);
    assert(ret == 0);
    
    buflen = bep44_signbuf(10, value, sizeof(value) - 1, buf, sizeof(buf));
    bf_sign(sig, buf, (size_t)buflen, sk);
    ret = recstore_accept(pk, 10, value, sizeof(value) - 1, sig);
    assert(ret == 0);
    
    buflen = bep44_signbuf(15, value, sizeof(value) - 1, buf, sizeof(buf));
    bf_sign(sig, buf, (size_t)buflen, sk);
    ret = recstore_accept(pk, 15, value, sizeof(value) - 1, sig);
    assert(ret == 1);
    
    unlink(path);
    printf("  test_accept_stale: OK\n");
}

static void test_get(void) {
    recstore_init(NULL);
    
    unsigned char target[20] = {0};
    rec_t out;
    
    int ret = recstore_get(target, &out);
    assert(ret == 0);
    
    printf("  test_get: OK\n");
}

static void test_get_null(void) {
    rec_t out;
    
    int ret = recstore_get(NULL, &out);
    assert(ret == 0);
    
    unsigned char target[20] = {0};
    ret = recstore_get(target, NULL);
    assert(ret == 0);
    
    printf("  test_get_null: OK\n");
}

static void test_get_by_pubkey(void) {
    recstore_init(NULL);
    
    unsigned char pk[32] = {0};
    rec_t out;
    
    int ret = recstore_get_by_pubkey(pk, &out);
    assert(ret == 0);
    
    printf("  test_get_by_pubkey: OK\n");
}

static void test_get_by_pubkey_after_accept(void) {
    const char *path = "/tmp/norn_test_recstore_pubkey";
    unlink(path);
    
    recstore_init(path);
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "test pubkey lookup";
    unsigned char buf[256];
    int buflen = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    
    unsigned char sig[64];
    bf_sign(sig, buf, (size_t)buflen, sk);
    
    int ret = recstore_accept(pk, 1, value, sizeof(value) - 1, sig);
    assert(ret == 1);
    
    rec_t out;
    ret = recstore_get_by_pubkey(pk, &out);
    assert(ret == 1);
    assert(memcmp(out.k, pk, 32) == 0);
    
    unsigned char pk2[32];
    crypto_sign_keypair(pk2, (unsigned char[64]){0});
    ret = recstore_get_by_pubkey(pk2, &out);
    assert(ret == 0);
    
    unlink(path);
    printf("  test_get_by_pubkey_after_accept: OK\n");
}

static void test_get_by_node_id(void) {
    recstore_init(NULL);
    
    unsigned char node_id[20] = {0};
    rec_t out;
    
    int ret = recstore_get_by_node_id(node_id, &out);
    assert(ret == 0);
    
    ret = recstore_get_by_node_id(NULL, &out);
    assert(ret == 0);
    
    printf("  test_get_by_node_id: OK\n");
}

static void test_set_via(void) {
    recstore_init(NULL);
    
    unsigned char pk[32] = {0};
    recstore_set_via(pk, "peer");
    
    printf("  test_set_via: OK\n");
}

static void test_set_via_after_accept(void) {
    const char *path = "/tmp/norn_test_recstore_via";
    unlink(path);
    
    recstore_init(path);
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "test via";
    unsigned char buf[256];
    int buflen = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    
    unsigned char sig[64];
    bf_sign(sig, buf, (size_t)buflen, sk);
    
    int ret = recstore_accept(pk, 1, value, sizeof(value) - 1, sig);
    assert(ret == 1);
    
    recstore_set_via(pk, "test_peer");
    
    unsigned char target[20];
    bep44_target(pk, target);
    
    rec_t out;
    ret = recstore_get(target, &out);
    assert(ret == 1);
    assert(strcmp(out.via, "test_peer") == 0);
    
    recstore_set_via(NULL, "test_peer");
    
    recstore_set_via(pk, NULL);
    
    unlink(path);
    printf("  test_set_via_after_accept: OK\n");
}

static void test_set_private(void) {
    recstore_init(NULL);
    
    unsigned char pk[32] = {0};
    recstore_set_private(pk, 1);
    
    recstore_set_private(NULL, 1);
    
    printf("  test_set_private: OK\n");
}

static void test_set_private_after_accept(void) {
    const char *path = "/tmp/norn_test_recstore_private";
    unlink(path);
    
    recstore_init(path);
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "test private";
    unsigned char buf[256];
    int buflen = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    
    unsigned char sig[64];
    bf_sign(sig, buf, (size_t)buflen, sk);
    
    int ret = recstore_accept(pk, 1, value, sizeof(value) - 1, sig);
    assert(ret == 1);
    
    recstore_set_private(pk, 1);
    
    unsigned char target[20];
    bep44_target(pk, target);
    
    rec_t out;
    ret = recstore_get(target, &out);
    assert(ret == 1);
    assert(out.priv == 1);
    
    recstore_set_private(pk, 0);
    
    ret = recstore_get(target, &out);
    assert(ret == 1);
    assert(out.priv == 0);
    
    unlink(path);
    printf("  test_set_private_after_accept: OK\n");
}

static void test_count(void) {
    recstore_init(NULL);
    
    int count = recstore_count();
    assert(count >= 0);
    
    printf("  test_count: OK\n");
}

static void test_list(void) {
    recstore_init(NULL);
    
    rec_t out[10];
    int count = recstore_list(out, 10);
    assert(count >= 0);
    assert(count <= 10);
    
    printf("  test_list: OK\n");
}

static void test_list_null(void) {
    int count = recstore_list(NULL, 10);
    assert(count == 0);
    
    rec_t out[10];
    count = recstore_list(out, 0);
    assert(count == 0);
    
    count = recstore_list(out, -1);
    assert(count == 0);
    
    printf("  test_list_null: OK\n");
}

static void test_save_load(void) {
    const char *path = "/tmp/norn_test_recstore_save";
    unlink(path);
    
    recstore_init(path);
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "persist me";
    unsigned char buf[256];
    int buflen = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    
    unsigned char sig[64];
    bf_sign(sig, buf, (size_t)buflen, sk);
    
    int ret = recstore_accept(pk, 1, value, sizeof(value) - 1, sig);
    assert(ret == 1);
    
    unsigned char target[20];
    bep44_target(pk, target);
    
    recstore_init(path);
    
    rec_t out;
    ret = recstore_get(target, &out);
    assert(ret == 1);
    assert(out.seq == 1);
    assert(out.vlen == sizeof(value) - 1);
    
    unlink(path);
    printf("  test_save_load: OK\n");
}

static void test_private_not_saved(void) {
    const char *path = "/tmp/norn_test_recstore_private_not_saved";
    unlink(path);
    
    recstore_init(path);
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "private record";
    unsigned char buf[256];
    int buflen = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    
    unsigned char sig[64];
    bf_sign(sig, buf, (size_t)buflen, sk);
    
    int ret = recstore_accept(pk, 1, value, sizeof(value) - 1, sig);
    assert(ret == 1);
    
    recstore_set_private(pk, 1);
    
    recstore_init(path);
    
    unsigned char target[20];
    bep44_target(pk, target);
    
    rec_t out;
    ret = recstore_get(target, &out);
    assert(ret == 0);
    
    unlink(path);
    printf("  test_private_not_saved: OK\n");
}

static void test_accept_max_capacity(void) {
    const char *path = "/tmp/norn_test_recstore_max";
    unlink(path);
    
    recstore_init(path);
    
    unsigned char value[] = "test";
    unsigned char buf[256];
    unsigned char sig[64];
    
    for (int i = 0; i < RECSTORE_MAX + 10; i++) {
        keypair_t kp;
        crypto_keypair_new(&kp);
        
        int buflen = bep44_signbuf((uint32_t)i + 1, value, sizeof(value) - 1, buf, sizeof(buf));
        bf_sign(sig, buf, (size_t)buflen, kp.secret_key);
        
        int ret = recstore_accept(kp.public_key, (uint32_t)i + 1, value, sizeof(value) - 1, sig);
        if (i < RECSTORE_MAX) {
            assert(ret == 1);
        } else {
            assert(ret == 0);
        }
    }
    
    assert(recstore_count() == RECSTORE_MAX);
    
    unlink(path);
    printf("  test_accept_max_capacity: OK\n");
}

static void test_load_malformed_file(void) {
    const char *path = "/tmp/norn_test_recstore_malformed";
    
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "malformed line without enough fields\n");
    fclose(f);
    
    int ret = recstore_init(path);
    assert(ret == 0);
    
    unlink(path);
    
    f = fopen(path, "w");
    assert(f);
    fprintf(f, "1234567890123456789012345678901234567890 1234567890123456789012345678901234567890123456789012345678901234 1 5 12345 abcdef1234567890 1609459200\n");
    fclose(f);
    
    ret = recstore_init(path);
    assert(ret == 0);
    
    unlink(path);
    printf("  test_load_malformed_file: OK\n");
}

static void test_get_by_node_id_with_record(void) {
    const char *path = "/tmp/norn_test_recstore_nodeid";
    unlink(path);
    
    recstore_init(path);
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    bep44_record_t rec;
    memset(&rec, 0, sizeof(rec));
    strcpy(rec.version, "0.1.0");
    rec.ip = 0x7f000001;
    rec.port = 8080;
    rec.caps = 1;
    memset(rec.node_id, 0x42, 20);
    
    unsigned char value[256];
    int vlen = bep44_record_encode(&rec, value, sizeof(value));
    assert(vlen > 0);
    
    unsigned char buf[256];
    int buflen = bep44_signbuf(1, value, (size_t)vlen, buf, sizeof(buf));
    
    unsigned char sig[64];
    bf_sign(sig, buf, (size_t)buflen, kp.secret_key);
    
    int ret = recstore_accept(kp.public_key, 1, value, (size_t)vlen, sig);
    assert(ret == 1);
    
    rec_t out;
    ret = recstore_get_by_node_id(rec.node_id, &out);
    assert(ret == 1);
    assert(memcmp(out.k, kp.public_key, 32) == 0);
    
    unsigned char wrong_id[20];
    memset(wrong_id, 0xFF, 20);
    ret = recstore_get_by_node_id(wrong_id, &out);
    assert(ret == 0);
    
    unlink(path);
    printf("  test_get_by_node_id_with_record: OK\n");
}

/* Helper: accept a fresh signed record under an in-memory or persistent store. */
static void accept_signed(const unsigned char *pk, const unsigned char *sk,
                          uint32_t seq, const unsigned char *value, size_t vlen) {
    unsigned char buf[64 + RECSTORE_VMAX];
    int buflen = bep44_signbuf(seq, value, vlen, buf, sizeof(buf));
    assert(buflen > 0);
    unsigned char sig[64];
    bf_sign(sig, buf, (size_t)buflen, sk);
    int ret = recstore_accept(pk, seq, value, vlen, sig);
    assert(ret == 1);
}

/* In-memory accept exercises save() returning early on the empty path (line 40),
 * and accept with vlen > RECSTORE_VMAX (line 87 vlen-too-big arm). */
static void test_accept_in_memory_and_oversize(void) {
    recstore_init(NULL);

    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);

    unsigned char value[] = "in memory record";
    accept_signed(pk, sk, 1, value, sizeof(value) - 1);  /* save() hits empty-path return */

    unsigned char target[20];
    bep44_target(pk, target);
    rec_t out;
    assert(recstore_get(target, &out) == 1);

    /* vlen > RECSTORE_VMAX is rejected up front */
    unsigned char big[RECSTORE_VMAX + 1] = {0};
    unsigned char sig[64] = {0};
    int ret = recstore_accept(pk, 2, big, sizeof(big), sig);
    assert(ret == 0);

    printf("  test_accept_in_memory_and_oversize: OK\n");
}

/* save() with a path in a non-existent directory: fopen of the .tmp file fails,
 * exercising the !f early return (line 43). make check stays green. */
static void test_save_fopen_fail(void) {
    const char *path = "/nonexistent_dir_norn_test/recstore.dat";
    int ret = recstore_init(path);
    assert(ret == 0);   /* fopen for read fails -> 0 records, path retained */

    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);

    unsigned char value[] = "no dir";
    accept_signed(pk, sk, 1, value, sizeof(value) - 1);  /* save() -> fopen tmp fails */

    unsigned char target[20];
    bep44_target(pk, target);
    rec_t out;
    assert(recstore_get(target, &out) == 1);  /* still held in memory */

    printf("  test_save_fopen_fail: OK\n");
}

/* recstore_get with out == NULL while a record exists (line 139 false arm). */
static void test_get_null_out_with_record(void) {
    recstore_init(NULL);

    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);

    unsigned char value[] = "present";
    accept_signed(pk, sk, 1, value, sizeof(value) - 1);

    unsigned char target[20];
    bep44_target(pk, target);
    int ret = recstore_get(target, NULL);
    assert(ret == 1);   /* found, but no copy-out */

    printf("  test_get_null_out_with_record: OK\n");
}

/* set_private when the flag already equals val (line 119: e->priv != val false). */
static void test_set_private_noop(void) {
    recstore_init(NULL);

    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);

    unsigned char value[] = "priv noop";
    accept_signed(pk, sk, 1, value, sizeof(value) - 1);

    /* fresh record is public (0); setting to 0 again is a no-op */
    recstore_set_private(pk, 0);

    unsigned char target[20];
    bep44_target(pk, target);
    rec_t out;
    assert(recstore_get(target, &out) == 1);
    assert(out.priv == 0);

    printf("  test_set_private_noop: OK\n");
}

/* recstore_list with records present (line 171 loop body executes). */
static void test_list_with_records(void) {
    recstore_init(NULL);

    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);

    unsigned char value[] = "listed";
    accept_signed(pk, sk, 1, value, sizeof(value) - 1);

    rec_t out[4];
    int n = recstore_list(out, 4);
    assert(n == 1);

    printf("  test_list_with_records: OK\n");
}

/* get_by_node_id: undecodeable value is skipped (line 157 continue), and a match
 * with out == NULL exercises line 159 false arm. */
static void test_get_by_node_id_branches(void) {
    recstore_init(NULL);

    /* Record 1: short value that fails bep44_record_decode -> skipped. */
    unsigned char pk1[32], sk1[64];
    crypto_sign_keypair(pk1, sk1);
    unsigned char shortv[] = "x";   /* too short to decode as a record */
    accept_signed(pk1, sk1, 1, shortv, sizeof(shortv) - 1);

    /* Record 2: a valid record carrying a node_id. */
    keypair_t kp;
    crypto_keypair_new(&kp);
    bep44_record_t rec;
    memset(&rec, 0, sizeof(rec));
    strcpy(rec.version, "0.1.0");
    rec.ip = 0x7f000001;
    rec.port = 9090;
    rec.caps = 1;
    memset(rec.node_id, 0x37, 20);

    unsigned char value[256];
    int vlen = bep44_record_encode(&rec, value, sizeof(value));
    assert(vlen > 0);
    accept_signed(kp.public_key, kp.secret_key, 1, value, (size_t)vlen);

    /* Match found but out == NULL (line 159 false arm). The decode of record 1
     * fails first, exercising the continue at line 157. */
    int ret = recstore_get_by_node_id(rec.node_id, NULL);
    assert(ret == 1);

    printf("  test_get_by_node_id_branches: OK\n");
}

/* Load paths: truncated/invalid hex (lines 24/27), vlen-too-big (line 74),
 * hexdec failures during load (lines 76/77), and the RECSTORE_MAX loop guard
 * (line 71 g_count<MAX false arm). */
static void test_load_branches(void) {
    const char *path = "/tmp/norn_test_recstore_loadbr";
    /* A valid 64-hex pubkey field and 128-hex sig field reused below. */
    const char *KK = "0000000000000000000000000000000000000000000000000000000000000000";
    const char *SG = "0000000000000000000000000000000000000000000000000000000000000000"
                     "0000000000000000000000000000000000000000000000000000000000000000";

    /* (a) vlen > RECSTORE_VMAX -> continue (line 74). */
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "%040d %s 1 99999 dead %s 100\n", 0, KK, SG);
    fclose(f);
    assert(recstore_init(path) == 0);

    /* (b) every invalid-hex sub-condition of line 27 via the target field, plus
     * line 24 truncation, all as separate lines parsed by one init pass. Each
     * makes hexdec(tg,...) fail at line 76. The leading char/second char drive
     * hi/lo through <0 and >15. */
    f = fopen(path, "w");
    assert(f);
    /* hi < 0  : first nibble ':' (=58) -> (':'|0x20)-'a'+10 < 0 */
    fprintf(f, ":000000000000000000000000000000000000000 %s 1 2 dead %s 100\n", KK, SG);
    /* hi > 15 : first nibble 'z' */
    fprintf(f, "z000000000000000000000000000000000000000 %s 1 2 dead %s 100\n", KK, SG);
    /* lo < 0  : second nibble ':' (first valid) */
    fprintf(f, "0:00000000000000000000000000000000000000 %s 1 2 dead %s 100\n", KK, SG);
    /* lo > 15 : second nibble 'z' (first valid) */
    fprintf(f, "0z00000000000000000000000000000000000000 %s 1 2 dead %s 100\n", KK, SG);
    fclose(f);
    assert(recstore_init(path) == 0);

    /* (c1) tg valid, kk invalid -> hexdec(kk) fails (line 76 second operand). */
    f = fopen(path, "w");
    assert(f);
    fprintf(f, "%040d zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz 1 2 dead %s 100\n", 0, SG);
    fclose(f);
    assert(recstore_init(path) == 0);

    /* (c2) tg,kk valid, value hex truncated: vlen=2 needs 4 chars, "ab" gives 2
     * -> hexdec(vv) reads '\0' (line 24) and fails (line 77 first operand). */
    f = fopen(path, "w");
    assert(f);
    fprintf(f, "%040d %s 1 2 ab %s 100\n", 0, KK, SG);
    fclose(f);
    assert(recstore_init(path) == 0);

    /* (c3) tg,kk,vv valid but sig hex invalid -> hexdec(sg) fails (line 77
     * second operand). vlen=2 with 4 valid hex chars "abcd". */
    f = fopen(path, "w");
    assert(f);
    fprintf(f, "%040d %s 1 2 abcd zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz 100\n", 0, KK);
    fclose(f);
    assert(recstore_init(path) == 0);

    /* (d) more than RECSTORE_MAX valid lines: the loop stops on g_count<MAX
     * (line 71 false arm). Generate RECSTORE_MAX+5 valid signed records. */
    f = fopen(path, "w");
    assert(f);
    unsigned char value[] = "L";
    for (int i = 0; i < RECSTORE_MAX + 5; i++) {
        keypair_t kp;
        crypto_keypair_new(&kp);
        unsigned char buf[64 + RECSTORE_VMAX];
        int buflen = bep44_signbuf((uint32_t)i + 1, value, sizeof(value) - 1, buf, sizeof(buf));
        unsigned char sig[64];
        bf_sign(sig, buf, (size_t)buflen, kp.secret_key);

        char tg[41], kk[65], vv[RECSTORE_VMAX * 2 + 1], sg[129];
        static const char hd[] = "0123456789abcdef";
        unsigned char target[20];
        bep44_target(kp.public_key, target);
        for (int j = 0; j < 20; j++) { tg[j*2] = hd[target[j] >> 4]; tg[j*2+1] = hd[target[j] & 0xf]; }
        tg[40] = '\0';
        for (int j = 0; j < 32; j++) { kk[j*2] = hd[kp.public_key[j] >> 4]; kk[j*2+1] = hd[kp.public_key[j] & 0xf]; }
        kk[64] = '\0';
        size_t vlen = sizeof(value) - 1;
        for (size_t j = 0; j < vlen; j++) { vv[j*2] = hd[value[j] >> 4]; vv[j*2+1] = hd[value[j] & 0xf]; }
        vv[vlen*2] = '\0';
        for (int j = 0; j < 64; j++) { sg[j*2] = hd[sig[j] >> 4]; sg[j*2+1] = hd[sig[j] & 0xf]; }
        sg[128] = '\0';
        fprintf(f, "%s %s %u %zu %s %s %ld\n", tg, kk, (unsigned)(i + 1), vlen, vv, sg, (long)100);
    }
    fclose(f);
    int loaded = recstore_init(path);
    assert(loaded == RECSTORE_MAX);

    unlink(path);
    printf("  test_load_branches: OK\n");
}

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    printf("test_recstore:\n");
    
    test_init();
    test_accept();
    test_accept_null();
    test_accept_signed();
    test_accept_stale();
    test_get();
    test_get_null();
    test_get_by_pubkey();
    test_get_by_pubkey_after_accept();
    test_get_by_node_id();
    test_set_via();
    test_set_via_after_accept();
    test_set_private();
    test_set_private_after_accept();
    test_count();
    test_list();
    test_list_null();
    test_save_load();
    test_private_not_saved();
    test_accept_max_capacity();
    test_get_by_node_id_with_record();
    test_load_malformed_file();
    test_accept_in_memory_and_oversize();
    test_save_fopen_fail();
    test_get_null_out_with_record();
    test_set_private_noop();
    test_list_with_records();
    test_get_by_node_id_branches();
    test_load_branches();

    printf("test_recstore: OK\n");
    return 0;
}