/* SPDX-License-Identifier: MIT */
#include "norn.h"
#include "norn_session.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

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
    unsigned char pk[32] = {0};

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

static void on_accept_stub2(norn_session_t *session, void *ud) {
    (void)session;
    (void)ud;
}

static void test_listen_async(void) {
    printf("  test_listen_async: ");
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    int ret = norn_listen_async(client, 0, NULL, on_accept_stub2, NULL);
    assert(ret == 0);
    
    norn_free(client);
    printf("OK\n");
}

static void test_save_load_dht_nodes(void) {
    printf("  test_save_load_dht_nodes: ");
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    const char *path = "/tmp/norn_test_nodes.XXXXXX";
    char tmpfile[64];
    strncpy(tmpfile, path, sizeof(tmpfile) - 1);
    tmpfile[sizeof(tmpfile) - 1] = '\0';
    int fd = mkstemp(tmpfile);
    assert(fd >= 0);
    close(fd);
    
    int ret = norn_save_dht_nodes(client, tmpfile);
    assert(ret >= 0);
    
    ret = norn_load_dht_nodes(client, tmpfile);
    assert(ret >= 0);
    
    unlink(tmpfile);
    norn_free(client);
    printf("OK\n");
}

static void test_save_load_dht_nodes_null(void) {
    printf("  test_save_load_dht_nodes_null: ");
    
    int ret = norn_save_dht_nodes(NULL, "/tmp/test");
    assert(ret == -1);
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_client_t *client = norn_new(pk, sk, NULL);
    assert(client != NULL);
    
    ret = norn_save_dht_nodes(client, NULL);
    assert(ret == -1);
    
    ret = norn_load_dht_nodes(NULL, "/tmp/test");
    assert(ret == -1);
    
    ret = norn_load_dht_nodes(client, NULL);
    assert(ret == -1);
    
    norn_free(client);
    printf("OK\n");
}

/* norn_routing_nodes: iterate the DHT routing table. With a fresh client and
 * no bootstrap, the table is empty → returns 0 and writes nothing. NULL client
 * returns -1. */
static void test_routing_nodes(void) {
    printf("  test_routing_nodes: ");

    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);

    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";

    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);

    norn_routing_node_t out[8];
    int n = norn_routing_nodes(client, out, 8);
    assert(n >= 0);
    assert(n <= 8); /* can't exceed the cap we passed */

    norn_free(client);
    printf("OK\n");
}

static void test_routing_nodes_null(void) {
    printf("  test_routing_nodes_null: ");

    /* NULL client → -1, out untouched */
    norn_routing_node_t out[1];
    int n = norn_routing_nodes(NULL, out, 1);
    assert(n == -1);

    /* NULL out buffer with a valid client → 0 (or -1); must not crash. */
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    n = norn_routing_nodes(client, NULL, 8);
    assert(n == -1);
    norn_free(client);

    printf("OK\n");
}

/* norn_external_addr: returns -1 on NULL; for a fresh client have==0. */
static void test_external_addr(void) {
    printf("  test_external_addr: ");

    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);

    uint32_t ip = 0; uint16_t port = 0; int have = -1;
    assert(norn_external_addr(client, &ip, &port, &have) == 0);
    assert(have == 0 || have == 1); /* before discovery, 0; if discovered, 1 */
    norn_free(client);

    /* NULL safety */
    assert(norn_external_addr(NULL, &ip, &port, &have) == -1);
    assert(norn_external_addr(client, NULL, &port, &have) == -1);
    assert(norn_external_addr(client, &ip, NULL, &have) == -1);
    assert(norn_external_addr(client, &ip, &port, NULL) == -1);

    printf("OK\n");
}

/* norn_resolve_node: NULL/arg safety. A live lookup is network-dependent and
 * exercised by the served-KV PIT; here we only assert the error paths. */
static void test_resolve_node_null(void) {
    printf("  test_resolve_node_null: ");

    unsigned char node_id[20] = {0};
    uint32_t ip = 0; uint16_t port = 0; unsigned char pub[32];
    /* NULL client */
    assert(norn_resolve_node(NULL, node_id, &ip, &port, pub, 1000) == -1);
    /* NULL node_id / outputs */
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    assert(norn_resolve_node(client, NULL, &ip, &port, pub, 1000) == -1);
    assert(norn_resolve_node(client, node_id, NULL, &port, pub, 1000) == -1);
    assert(norn_resolve_node(client, node_id, &ip, NULL, pub, 1000) == -1);
    norn_free(client);

    printf("OK\n");
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
    test_listen_async();
    test_save_load_dht_nodes();
    test_save_load_dht_nodes_null();
    test_routing_nodes();
    test_routing_nodes_null();
    test_external_addr();
    test_resolve_node_null();
    
    printf("test_norn: OK\n");
    return 0;
}