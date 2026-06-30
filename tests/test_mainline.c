/* SPDX-License-Identifier: MIT */
#include "mainline.h"
#include "net.h"
#include "crypto.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>

static void test_init_cleanup(void) {
    net_t net;
    assert(net_init(&net, 0) == 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    
    int ret = mainline_init(&state, &net, key);
    assert(ret == 0);
    assert(state.net == &net);
    assert(state.node_count == 0);
    assert(state.transaction_count == 0);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_init_cleanup: OK\n");
}

static void test_init_null(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32] = {0};
    
    int ret = mainline_init(NULL, &net, key);
    assert(ret == -1);
    
    ret = mainline_init(&state, NULL, key);
    assert(ret == -1);
    
    ret = mainline_init(&state, &net, NULL);
    assert(ret == -1);
    
    mainline_cleanup(NULL);
    net_cleanup(&net);
    printf("  test_init_null: OK\n");
}

static void test_add_node_basic(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    unsigned char node_id[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    uint32_t ip = 0x01020304;
    uint16_t port = 6881;
    
    int ret = mainline_add_node(&state, node_id, ip, port);
    assert(ret == 1);
    assert(state.node_count == 1);
    assert(memcmp(state.nodes[0].id, node_id, 20) == 0);
    assert(state.nodes[0].ip == ip);
    assert(state.nodes[0].port == port);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_add_node_basic: OK\n");
}

static void test_add_node_null(void) {
    mainline_state_t state;
    unsigned char node_id[20] = {0};
    uint32_t ip = 0x01020304;
    uint16_t port = 6881;
    
    int ret = mainline_add_node(NULL, node_id, ip, port);
    assert(ret == -1);
    
    ret = mainline_add_node(&state, NULL, ip, port);
    assert(ret == -1);
    
    printf("  test_add_node_null: OK\n");
}

static void test_add_node_duplicate(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    unsigned char node_id[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    
    int ret = mainline_add_node(&state, node_id, 0x01020304, 6881);
    assert(ret == 1);
    assert(state.node_count == 1);
    
    ret = mainline_add_node(&state, node_id, 0x05060708, 6882);
    assert(ret == 0);
    assert(state.node_count == 1);
    assert(state.nodes[0].ip == 0x05060708);
    assert(state.nodes[0].port == 6882);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_add_node_duplicate: OK\n");
}

static void test_add_node_self(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    int ret = mainline_add_node(&state, state.self_id, 0x01020304, 6881);
    assert(ret == -1);
    assert(state.node_count == 0);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_add_node_self: OK\n");
}

static void test_add_node_invalid(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    unsigned char node_id[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    
    int ret = mainline_add_node(&state, node_id, 0, 6881);
    assert(ret == -1);
    
    ret = mainline_add_node(&state, node_id, 0x01020304, 0);
    assert(ret == -1);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_add_node_invalid: OK\n");
}

static void test_get_node_count(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    assert(mainline_get_node_count(NULL) == 0);
    assert(mainline_get_node_count(&state) == 0);
    
    unsigned char node_id[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    mainline_add_node(&state, node_id, 0x01020304, 6881);
    assert(mainline_get_node_count(&state) == 1);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_get_node_count: OK\n");
}

static void test_evict_stale(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    assert(mainline_evict_stale(NULL, 60) == 0);
    
    unsigned char node_id[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    mainline_add_node(&state, node_id, 0x01020304, 6881);
    assert(state.node_count == 1);
    
    state.nodes[0].last_seen = time(NULL) - 3600;
    int evicted = mainline_evict_stale(&state, 1800);
    assert(evicted == 1);
    assert(state.node_count == 0);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_evict_stale: OK\n");
}

static void test_peer_cache_basic(void) {
    peer_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    
    unsigned char key[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    uint32_t ip = 0x01020304;
    uint16_t port = 6881;
    
    int ret = peer_cache_get(NULL, key, &ip, &port, 0);
    assert(ret == 0);
    
    ret = peer_cache_get(&cache, NULL, &ip, &port, 0);
    assert(ret == 0);
    
    peer_cache_put(&cache, key, ip, port);
    assert(cache.count == 1);
    
    uint32_t out_ip;
    uint16_t out_port;
    ret = peer_cache_get(&cache, key, &out_ip, &out_port, 0);
    assert(ret == 1);
    assert(out_ip == ip);
    assert(out_port == port);
    
    printf("  test_peer_cache_basic: OK\n");
}

static void test_peer_cache_update(void) {
    peer_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    
    unsigned char key[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    
    peer_cache_put(&cache, key, 0x01020304, 6881);
    assert(cache.count == 1);
    
    peer_cache_put(&cache, key, 0x05060708, 6882);
    assert(cache.count == 1);
    
    uint32_t out_ip;
    uint16_t out_port;
    int ret = peer_cache_get(&cache, key, &out_ip, &out_port, 0);
    assert(ret == 1);
    assert(out_ip == 0x05060708);
    assert(out_port == 6882);
    
    printf("  test_peer_cache_update: OK\n");
}

static void test_peer_cache_null(void) {
    peer_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    
    unsigned char key[20] = {0};
    
    peer_cache_put(NULL, key, 0x01020304, 6881);
    
    peer_cache_put(&cache, NULL, 0x01020304, 6881);
    assert(cache.count == 0);
    
    peer_cache_put(&cache, key, 0, 6881);
    assert(cache.count == 0);
    
    peer_cache_put(&cache, key, 0x01020304, 0);
    assert(cache.count == 0);
    
    printf("  test_peer_cache_null: OK\n");
}

static void test_peer_cache_age(void) {
    peer_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    
    unsigned char key[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    
    peer_cache_put(&cache, key, 0x01020304, 6881);
    
    uint32_t out_ip;
    uint16_t out_port;
    int ret = peer_cache_get(&cache, key, &out_ip, &out_port, 3600);
    assert(ret == 1);
    
    cache.entries[0].updated = time(NULL) - 7200;
    ret = peer_cache_get(&cache, key, &out_ip, &out_port, 3600);
    assert(ret == 0);
    
    printf("  test_peer_cache_age: OK\n");
}

static void test_save_load_nodes(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    const char *path = "/tmp/test_norn_nodes.bin";
    unlink(path);
    
    int ret = mainline_save_nodes(NULL, path);
    assert(ret == -1);
    
    ret = mainline_save_nodes(&state, NULL);
    assert(ret == -1);
    
    ret = mainline_save_nodes(&state, path);
    assert(ret == 0);
    
    ret = mainline_load_nodes(NULL, path);
    assert(ret == -1);
    
    ret = mainline_load_nodes(&state, NULL);
    assert(ret == -1);
    
    ret = mainline_load_nodes(&state, path);
    assert(ret == 0);
    
    unlink(path);
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_save_load_nodes: OK\n");
}

static void test_save_load_with_nodes(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    unsigned char node_id1[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    unsigned char node_id2[20] = {2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21};
    
    mainline_add_node(&state, node_id1, 0x01020304, 6881);
    mainline_add_node(&state, node_id2, 0x05060708, 6882);
    assert(state.node_count == 2);
    
    const char *path = "/tmp/test_norn_nodes2.bin";
    int ret = mainline_save_nodes(&state, path);
    assert(ret == 2);
    
    mainline_state_t state2;
    mainline_init(&state2, &net, key);
    
    ret = mainline_load_nodes(&state2, path);
    assert(ret == 2);
    assert(state2.node_count == 2);
    
    unlink(path);
    mainline_cleanup(&state);
    mainline_cleanup(&state2);
    net_cleanup(&net);
    printf("  test_save_load_with_nodes: OK\n");
}

static void test_peer_cache_save_load(void) {
    peer_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    
    const char *path = "/tmp/test_norn_peers.bin";
    unlink(path);
    
    int ret = peer_cache_save(NULL, path);
    assert(ret == -1);
    
    ret = peer_cache_save(&cache, NULL);
    assert(ret == -1);
    
    unsigned char key[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    peer_cache_put(&cache, key, 0x01020304, 6881);
    
    ret = peer_cache_save(&cache, path);
    assert(ret == 1);
    
    peer_cache_t cache2;
    memset(&cache2, 0, sizeof(cache2));
    
    ret = peer_cache_load(NULL, path);
    assert(ret == -1);
    
    ret = peer_cache_load(&cache2, NULL);
    assert(ret == -1);
    
    ret = peer_cache_load(&cache2, path);
    assert(ret == 1);
    assert(cache2.count == 1);
    
    unlink(path);
    printf("  test_peer_cache_save_load: OK\n");
}

static void test_set_logger(void) {
    mainline_set_logger(NULL);
    mainline_set_logger(NULL);
    printf("  test_set_logger: OK\n");
}

static void test_add_bootstrap(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    mainline_add_bootstrap(&state, 0x01020304, 6881);
    assert(state.boot_count == 1);
    assert(state.boot_ips[0] == 0x01020304);
    assert(state.boot_ports[0] == 6881);
    
    mainline_add_bootstrap(&state, 0x05060708, 6882);
    assert(state.boot_count == 2);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_add_bootstrap: OK\n");
}

static void test_set_private(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    assert(state.private_mode == 0);
    
    mainline_set_private(&state, 1);
    assert(state.private_mode == 1);
    
    mainline_set_private(&state, 0);
    assert(state.private_mode == 0);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_set_private: OK\n");
}

static void test_set_read_only(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    assert(state.read_only == 0);
    
    mainline_set_read_only(&state, 1);
    assert(state.read_only == 1);
    
    mainline_set_read_only(&state, 0);
    assert(state.read_only == 0);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_set_read_only: OK\n");
}

static void test_needs_bootstrap(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    state.last_bootstrap = 0;
    assert(mainline_needs_bootstrap(&state) == 1);
    
    state.last_bootstrap = time(NULL);
    assert(mainline_needs_bootstrap(&state) == 0);
    
    state.last_bootstrap = time(NULL) - 400;
    assert(mainline_needs_bootstrap(&state) == 1);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_needs_bootstrap: OK\n");
}

static void test_find_node(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    unsigned char node_id[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    mainline_add_node(&state, node_id, 0x01020304, 6881);
    
    int ret = mainline_find_node(NULL, node_id, 0x01020304, 6881);
    assert(ret == -1);
    
    ret = mainline_find_node(&state, NULL, 0x01020304, 6881);
    assert(ret == -1);
    
    ret = mainline_find_node(&state, node_id, 0x01020304, 6881);
    assert(ret == 0);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_find_node: OK\n");
}

static void test_get_bootstrap_nodes(void) {
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    uint32_t ips[8];
    uint16_t ports[8];
    
    int ret = mainline_get_bootstrap_nodes(NULL, ips, ports, 8);
    assert(ret == 0);
    
    ret = mainline_get_bootstrap_nodes(&state, NULL, ports, 8);
    assert(ret == 0);
    
    ret = mainline_get_bootstrap_nodes(&state, ips, NULL, 8);
    assert(ret == 0);
    
    ret = mainline_get_bootstrap_nodes(&state, ips, ports, 0);
    assert(ret == 0);
    
    ret = mainline_get_bootstrap_nodes(&state, ips, ports, 8);
    assert(ret == 3);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("  test_get_bootstrap_nodes: OK\n");
}

static void test_bootstrap_local(void) {
    printf("  test_bootstrap_local: ");
    
    net_t net;
    net_init(&net, 0);
    
    mainline_state_t state;
    unsigned char key[32];
    crypto_sign_keypair(key, (unsigned char[64]){0});
    mainline_init(&state, &net, key);
    
    int ret = mainline_bootstrap(&state);
    assert(ret == 0);
    assert(state.last_bootstrap > 0);
    
    mainline_cleanup(&state);
    net_cleanup(&net);
    printf("OK\n");
}

static void test_bootstrap_local_null(void) {
    printf("  test_bootstrap_local_null: ");
    
    int ret = mainline_bootstrap(NULL);
    assert(ret == -1);
    
    printf("OK\n");
}

int main(void) {
    printf("=== test_mainline.c ===\n");
    
    if (sodium_init() < 0) {
        fprintf(stderr, "sodium_init failed\n");
        return 1;
    }
    
    test_init_cleanup();
    test_init_null();
    test_add_node_basic();
    test_add_node_null();
    test_add_node_duplicate();
    test_add_node_self();
    test_add_node_invalid();
    test_get_node_count();
    test_evict_stale();
    test_peer_cache_basic();
    test_peer_cache_update();
    test_peer_cache_null();
    test_peer_cache_age();
    test_save_load_nodes();
    test_save_load_with_nodes();
    test_peer_cache_save_load();
    test_set_logger();
    test_add_bootstrap();
    test_bootstrap_local();
    test_bootstrap_local_null();
    test_set_private();
    test_set_read_only();
    test_needs_bootstrap();
    test_find_node();
    test_get_bootstrap_nodes();
    
    printf("=== All tests passed ===\n");
    return 0;
}