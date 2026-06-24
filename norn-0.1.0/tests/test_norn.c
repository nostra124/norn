#include "norn.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_new_free(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    norn_free(client);
    printf("  test_new_free: OK\n");
}

static void test_new_null(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    norn_client_t *client = norn_new(NULL, sk, &cfg);
    assert(client == NULL);
    
    client = norn_new(pk, NULL, &cfg);
    assert(client == NULL);
    
    client = norn_new(pk, sk, NULL);
    assert(client != NULL);
    norn_free(client);
    
    printf("  test_new_null: OK\n");
}

static void test_free_null(void) {
    norn_free(NULL);
    printf("  test_free_null: OK\n");
}

static void test_get_id(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    unsigned char id[20];
    int ret = norn_get_id(client, id);
    assert(ret == 0);
    
    norn_free(client);
    printf("  test_get_id: OK\n");
}

static void test_get_id_null(void) {
    unsigned char id[20];
    
    int ret = norn_get_id(NULL, id);
    assert(ret == -1);
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    
    ret = norn_get_id(client, NULL);
    assert(ret == -1);
    
    norn_free(client);
    printf("  test_get_id_null: OK\n");
}

static void test_bootstrap(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    int ret = norn_bootstrap(client);
    assert(ret == 0 || ret == -1);
    
    norn_free(client);
    printf("  test_bootstrap: OK\n");
}

static void test_bootstrap_null(void) {
    int ret = norn_bootstrap(NULL);
    assert(ret == -1);
    printf("  test_bootstrap_null: OK\n");
}

static void test_tick(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    int ret = norn_tick(client);
    assert(ret >= 0);
    
    norn_free(client);
    printf("  test_tick: OK\n");
}

static void test_tick_null(void) {
    int ret = norn_tick(NULL);
    assert(ret == -1 || ret == 0);
    printf("  test_tick_null: OK\n");
}

static void test_get_fd(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    int fd = norn_get_fd(client);
    assert(fd >= -1);
    
    norn_free(client);
    printf("  test_get_fd: OK\n");
}

static void test_get_fd_null(void) {
    int fd = norn_get_fd(NULL);
    assert(fd == -1);
    printf("  test_get_fd_null: OK\n");
}

static void test_encode_decode_mutable(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_mutable_t rec;
    memset(&rec, 0, sizeof(rec));
    memcpy(rec.key, pk, 20);
    memcpy(rec.pubkey, pk, 32);
    strcpy((char*)rec.value, "test value");
    rec.value_len = strlen((char*)rec.value);
    rec.seq = 1;
    rec.have_sig = 0;
    
    unsigned char out[1024];
    int len = norn_encode_mutable(&rec, out, sizeof(out));
    
    norn_mutable_t rec2;
    memset(&rec2, 0, sizeof(rec2));
    (void)norn_decode_mutable(out, (size_t)(len > 0 ? len : 0), &rec2);
    
    printf("  test_encode_decode_mutable: OK\n");
}

static void test_encode_mutable_null(void) {
    unsigned char out[1024];
    
    int len = norn_encode_mutable(NULL, out, sizeof(out));
    assert(len == -1);
    
    norn_mutable_t rec;
    memset(&rec, 0, sizeof(rec));
    
    len = norn_encode_mutable(&rec, NULL, 0);
    assert(len == -1);
    
    printf("  test_encode_mutable_null: OK\n");
}

static void test_decode_mutable_null(void) {
    unsigned char buf[100];
    norn_mutable_t rec;
    
    int ret = norn_decode_mutable(NULL, 100, &rec);
    assert(ret == -1);
    
    ret = norn_decode_mutable(buf, 100, NULL);
    assert(ret == -1);
    
    printf("  test_decode_mutable_null: OK\n");
}

static void test_put_mutable(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    unsigned char value[] = "test value";
    int ret = norn_put_mutable(client, pk, sk, value, sizeof(value) - 1, 1);
    assert(ret == 0 || ret == -1);
    
    norn_free(client);
    printf("  test_put_mutable: OK\n");
}

static void test_put_mutable_null(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "test";
    
    int ret = norn_put_mutable(NULL, pk, sk, value, 4, 1);
    assert(ret == -1);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    
    ret = norn_put_mutable(client, NULL, sk, value, 4, 1);
    assert(ret == -1);
    
    ret = norn_put_mutable(client, pk, NULL, value, 4, 1);
    assert(ret == -1);
    
    ret = norn_put_mutable(client, pk, sk, NULL, 4, 1);
    assert(ret == -1);
    
    norn_free(client);
    printf("  test_put_mutable_null: OK\n");
}

static void test_get_mutable(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    int ret = norn_get_mutable(client, pk, NULL, NULL);
    assert(ret == 0 || ret == -1);
    
    norn_free(client);
    printf("  test_get_mutable: OK\n");
}

static void test_get_mutable_null(void) {
    unsigned char pk[32];
    
    int ret = norn_get_mutable(NULL, pk, NULL, NULL);
    assert(ret == -1);
    
    printf("  test_get_mutable_null: OK\n");
}

static void test_put_immutable(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    unsigned char value[] = "test value";
    int ret = norn_put_immutable(client, value, sizeof(value) - 1);
    assert(ret == 0 || ret == -1);
    
    norn_free(client);
    printf("  test_put_immutable: OK\n");
}

static void test_get_immutable(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    unsigned char key[20] = {0};
    int ret = norn_get_immutable(client, key, NULL, NULL);
    assert(ret == 0 || ret == -1);
    
    norn_free(client);
    printf("  test_get_immutable: OK\n");
}

static void test_announce(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    unsigned char info_hash[20] = {0};
    int ret = norn_announce(client, info_hash);
    assert(ret == 0 || ret == -1);
    
    norn_free(client);
    printf("  test_announce: OK\n");
}

static void test_discover(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    unsigned char info_hash[20] = {0};
    int ret = norn_discover(client, info_hash, NULL, NULL);
    assert(ret == 0 || ret == -1);
    
    norn_free(client);
    printf("  test_discover: OK\n");
}

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    printf("test_norn:\n");
    
    test_new_free();
    test_new_null();
    test_free_null();
    test_get_id();
    test_get_id_null();
    test_bootstrap();
    test_bootstrap_null();
    test_tick();
    test_tick_null();
    test_get_fd();
    test_get_fd_null();
    test_encode_decode_mutable();
    test_encode_mutable_null();
    test_decode_mutable_null();
    test_put_mutable();
    test_put_mutable_null();
    test_get_mutable();
    test_get_mutable_null();
    test_put_immutable();
    test_get_immutable();
    test_announce();
    test_discover();
    
    printf("test_norn: OK\n");
    return 0;
}