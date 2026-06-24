#include "crypto.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

static void test_init(void) {
    int ret = crypto_init();
    assert(ret == 0 || ret == 1);  /* 0 = first init, 1 = already initialized */
    printf("  test_init: OK\n");
}

static void test_keypair_new(void) {
    keypair_t kp;
    int ret = crypto_keypair_new(&kp);
    assert(ret == 0);
    assert(memcmp(kp.public_key, kp.secret_key + 32, 32) == 0);
    printf("  test_keypair_new: OK\n");
}

static void test_keypair_new_null(void) {
    int ret = crypto_keypair_new(NULL);
    assert(ret == -1);
    printf("  test_keypair_new_null: OK\n");
}

static void test_keypair_save_load(void) {
    const char *path = "/tmp/norn_test_keypair";
    unlink(path);
    
    keypair_t kp;
    int ret = crypto_keypair_new(&kp);
    assert(ret == 0);
    
    ret = crypto_keypair_save(NULL, path);
    assert(ret == -1);
    
    ret = crypto_keypair_save(&kp, NULL);
    assert(ret == -1);
    
    ret = crypto_keypair_save(&kp, path);
    assert(ret == 0);
    
    keypair_t kp2;
    ret = crypto_keypair_load(NULL, path);
    assert(ret == -1);
    
    ret = crypto_keypair_load(&kp2, NULL);
    assert(ret == -1);
    
    ret = crypto_keypair_load(&kp2, path);
    assert(ret == 0);
    assert(memcmp(kp.public_key, kp2.public_key, 32) == 0);
    assert(memcmp(kp.secret_key, kp2.secret_key, 64) == 0);
    
    unlink(path);
    printf("  test_keypair_save_load: OK\n");
}

static void test_keypair_load_short(void) {
    const char *path = "/tmp/norn_test_keypair_short";
    unlink(path);
    
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite("short", 1, 5, f);
    fclose(f);
    
    keypair_t kp;
    int ret = crypto_keypair_load(&kp, path);
    assert(ret == -1);
    
    unlink(path);
    printf("  test_keypair_load_short: OK\n");
}

static void test_keypair_load_secret_only(void) {
    const char *path = "/tmp/norn_test_keypair_secret";
    unlink(path);
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite(kp.secret_key, 1, 64, f);
    fclose(f);
    
    keypair_t kp2;
    int ret = crypto_keypair_load(&kp2, path);
    assert(ret == 0);
    assert(memcmp(kp.public_key, kp2.public_key, 32) == 0);
    
    unlink(path);
    printf("  test_keypair_load_secret_only: OK\n");
}

static void test_keypair_save_short_write(void) {
    const char *path = "/tmp/norn_test_keypair_short_write";
    unlink(path);
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    int ret = crypto_keypair_save(&kp, path);
    assert(ret == 0);
    
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    assert(size == CRYPTO_SECRETKEYBYTES + CRYPTO_PUBLICKEYBYTES);
    
    unlink(path);
    printf("  test_keypair_save_short_write: OK\n");
}

static void test_keypair_load_error_paths(void) {
    const char *path = "/tmp/norn_test_keypair_err";
    keypair_t kp;
    
    int ret = crypto_keypair_load(&kp, "/nonexistent/path/key");
    assert(ret == -1);
    
    FILE *f = fopen(path, "wb");
    fprintf(f, "short");
    fclose(f);
    
    ret = crypto_keypair_load(&kp, path);
    assert(ret == -1);
    
    unlink(path);
    printf("  test_keypair_load_error_paths: OK\n");
}

static void test_bf_sign_verify(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char msg[] = "test message";
    unsigned char sig[64];
    
    int ret = bf_sign(NULL, msg, sizeof(msg) - 1, sk);
    assert(ret == -1);
    
    ret = bf_sign(sig, NULL, sizeof(msg) - 1, sk);
    assert(ret == -1);
    
    ret = bf_sign(sig, msg, sizeof(msg) - 1, NULL);
    assert(ret == -1);
    
    ret = bf_sign(sig, msg, sizeof(msg) - 1, sk);
    assert(ret == 0);
    
    ret = bf_verify(NULL, msg, sizeof(msg) - 1, pk);
    assert(ret == -1);
    
    ret = bf_verify(sig, NULL, sizeof(msg) - 1, pk);
    assert(ret == -1);
    
    ret = bf_verify(sig, msg, sizeof(msg) - 1, NULL);
    assert(ret == -1);
    
    ret = bf_verify(sig, msg, sizeof(msg) - 1, pk);
    assert(ret == 0);
    
    unsigned char bad_sig[64] = {0};
    ret = bf_verify(bad_sig, msg, sizeof(msg) - 1, pk);
    assert(ret == -1);
    
    printf("  test_bf_sign_verify: OK\n");
}

static void test_bf_seal(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char pt[] = "secret message";
    unsigned char ct[256];
    
    int ret = bf_seal(NULL, pt, sizeof(pt) - 1, ct);
    assert(ret == -1);
    
    ret = bf_seal(pk, NULL, sizeof(pt) - 1, ct);
    assert(ret == -1);
    
    ret = bf_seal(pk, pt, sizeof(pt) - 1, NULL);
    assert(ret == -1);
    
    ret = bf_seal(pk, pt, sizeof(pt) - 1, ct);
    assert(ret == 0);
    
    printf("  test_bf_seal: OK\n");
}

static void test_bf_seal_open(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char pt[] = "secret message";
    unsigned char ct[256];
    unsigned char out[256];
    
    bf_seal(pk, pt, sizeof(pt) - 1, ct);
    size_t ctlen = sizeof(pt) - 1 + 48;  /* plaintext + SEALBYTES */
    
    int ret = bf_seal_open(NULL, sk, ct, ctlen, out);
    assert(ret == -1);
    
    ret = bf_seal_open(pk, NULL, ct, ctlen, out);
    assert(ret == -1);
    
    ret = bf_seal_open(pk, sk, NULL, ctlen, out);
    assert(ret == -1);
    
    ret = bf_seal_open(pk, sk, ct, 0, out);
    assert(ret == -1);
    
    ret = bf_seal_open(pk, sk, ct, ctlen, NULL);
    assert(ret == -1);
    
    ret = bf_seal_open(pk, sk, ct, ctlen, out);
    assert(ret == 0);
    assert(memcmp(pt, out, sizeof(pt) - 1) == 0);
    
    printf("  test_bf_seal_open: OK\n");
}

static void test_bf_hash_name(void) {
    unsigned char hash[32];
    
    int ret = bf_hash_name(NULL, hash);
    assert(ret == -1);
    
    ret = bf_hash_name("test", NULL);
    assert(ret == -1);
    
    ret = bf_hash_name("test", hash);
    assert(ret == 0);
    
    unsigned char hash2[32];
    bf_hash_name("test", hash2);
    assert(memcmp(hash, hash2, 32) == 0);
    
    bf_hash_name("other", hash2);
    assert(memcmp(hash, hash2, 32) != 0);
    
    printf("  test_bf_hash_name: OK\n");
}

static void test_bf_is_fqdn(void) {
    int ret = bf_is_fqdn(NULL);
    assert(ret == 0);
    
    ret = bf_is_fqdn("");
    assert(ret == 0);
    
    ret = bf_is_fqdn("localhost");
    assert(ret == 0);
    
    ret = bf_is_fqdn(".example.com");
    assert(ret == 0);
    
    ret = bf_is_fqdn("example.com.");
    assert(ret == 0);
    
    ret = bf_is_fqdn("example..com");
    assert(ret == 0);
    
    ret = bf_is_fqdn("example.com");
    assert(ret == 1);
    
    ret = bf_is_fqdn("sub.example.com");
    assert(ret == 1);
    
    printf("  test_bf_is_fqdn: OK\n");
}

static void test_crc32c(void) {
    unsigned char data[] = "test";
    uint32_t crc = crypto_crc32c(data, 4);
    assert(crc != 0);
    
    unsigned char data2[] = "test";
    uint32_t crc2 = crypto_crc32c(data2, 4);
    assert(crc == crc2);
    
    unsigned char data3[] = "TEST";
    uint32_t crc3 = crypto_crc32c(data3, 4);
    assert(crc != crc3);
    
    printf("  test_crc32c: OK\n");
}

static void test_xor_distance(void) {
    unsigned char a[32] = {0xFF, 0xFF, 0xFF};
    unsigned char b[32] = {0x0F, 0x0F, 0x0F};
    unsigned char result[32];
    
    int ret = crypto_xor_distance(NULL, b, result, 32);
    assert(ret == -1);
    
    ret = crypto_xor_distance(a, NULL, result, 32);
    assert(ret == -1);
    
    ret = crypto_xor_distance(a, b, NULL, 32);
    assert(ret == -1);
    
    ret = crypto_xor_distance(a, b, result, 32);
    assert(ret == 0);
    assert(result[0] == 0xF0);
    assert(result[1] == 0xF0);
    assert(result[2] == 0xF0);
    
    printf("  test_xor_distance: OK\n");
}

static void test_compare_distance(void) {
    unsigned char a[32] = {0};
    unsigned char b[32] = {0};
    
    int ret = crypto_compare_distance(a, b);
    assert(ret == 0);
    
    a[0] = 1;
    ret = crypto_compare_distance(a, b);
    assert(ret == 1);
    
    b[0] = 2;
    ret = crypto_compare_distance(a, b);
    assert(ret == -1);
    
    printf("  test_compare_distance: OK\n");
}

static void test_generate_node_id(void) {
    unsigned char node_id[32];
    unsigned char pubkey[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29};
    
    int ret = crypto_generate_node_id(NULL, 0x01020304, 42, pubkey);
    assert(ret == -1);
    
    ret = crypto_generate_node_id(node_id, 0x01020304, 42, NULL);
    assert(ret == -1);
    
    ret = crypto_generate_node_id(node_id, 0x01020304, 42, pubkey);
    assert(ret == 0);
    
    assert(node_id[0] != 0 || node_id[1] != 0);
    
    printf("  test_generate_node_id: OK\n");
}

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    printf("test_crypto:\n");
    
    test_init();
    test_keypair_new();
    test_keypair_new_null();
    test_keypair_save_load();
    test_keypair_load_short();
    test_keypair_load_secret_only();
    test_keypair_save_short_write();
    test_keypair_load_error_paths();
    test_bf_sign_verify();
    test_bf_seal();
    test_bf_seal_open();
    test_bf_hash_name();
    test_bf_is_fqdn();
    test_crc32c();
    test_xor_distance();
    test_compare_distance();
    test_generate_node_id();
    
    printf("test_crypto: OK\n");
    return 0;
}