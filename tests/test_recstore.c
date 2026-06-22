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
    
    printf("test_recstore: OK\n");
    return 0;
}