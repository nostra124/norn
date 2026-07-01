/* SPDX-License-Identifier: MIT */
#include "kademlia.h"
#include "crypto.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_init_cleanup(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    int ret = kad_init(&state, &kp, 0x0A000001);
    assert(ret == 0);
    
    kad_cleanup(&state);
    printf("  test_init_cleanup: OK\n");
}

static void test_init_null(void) {
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    int ret = kad_init(NULL, &kp, 0);
    assert(ret == -1);
    
    kad_state_t state;
    ret = kad_init(&state, NULL, 0);
    assert(ret == -1);
    
    printf("  test_init_null: OK\n");
}

static void test_cleanup_null(void) {
    kad_cleanup(NULL);
    printf("  test_cleanup_null: OK\n");
}

static void test_update_node(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0x0A000001);
    
    unsigned char node_id[NODE_ID_BYTES];
    memset(node_id, 1, NODE_ID_BYTES);
    
    int ret = kad_update_node(&state, node_id, 0x0A000002, 6881, 0);
    assert(ret >= 0);
    
    size_t size = kad_routing_table_size(&state);
    assert(size >= 1);
    
    kad_cleanup(&state);
    printf("  test_update_node: OK\n");
}

static void test_update_node_null(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0);
    
    unsigned char node_id[NODE_ID_BYTES];
    int ret = kad_update_node(NULL, node_id, 0, 0, 0);
    assert(ret == -1);
    
    ret = kad_update_node(&state, NULL, 0, 0, 0);
    assert(ret == -1);
    
    kad_cleanup(&state);
    printf("  test_update_node_null: OK\n");
}

static void test_get_bucket_index(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0);
    
    unsigned char node_id[NODE_ID_BYTES];
    memcpy(node_id, state.self_id, NODE_ID_BYTES);
    node_id[0] ^= 0x80;
    
    int idx = kad_get_bucket_index(&state, node_id);
    assert(idx >= 0);
    assert(idx < MAX_BUCKETS);
    
    kad_cleanup(&state);
    printf("  test_get_bucket_index: OK\n");
}

static void test_get_bucket_index_null(void) {
    unsigned char node_id[NODE_ID_BYTES] = {0};
    int idx = kad_get_bucket_index(NULL, node_id);
    assert(idx < 0);

    /* NULL id with valid state: exercises the !id arm of the guard. */
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0);
    idx = kad_get_bucket_index(&state, NULL);
    assert(idx < 0);
    kad_cleanup(&state);

    printf("  test_get_bucket_index_null: OK\n");
}

static void test_routing_table_size(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0);
    
    size_t size = kad_routing_table_size(&state);
    assert(size == 0);
    
    unsigned char node_id[NODE_ID_BYTES];
    memset(node_id, 0, NODE_ID_BYTES);
    
    kad_update_node(&state, node_id, 0x0A000001, 6881, 0);
    
    size = kad_routing_table_size(&state);
    assert(size >= 1);
    
    kad_cleanup(&state);
    printf("  test_routing_table_size: OK\n");
}

static void test_routing_table_size_null(void) {
    size_t size = kad_routing_table_size(NULL);
    assert(size == 0);
    printf("  test_routing_table_size_null: OK\n");
}

static void test_refresh_buckets(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0);
    
    kad_refresh_buckets(&state);
    
    kad_cleanup(&state);
    printf("  test_refresh_buckets: OK\n");
}

static void test_refresh_buckets_null(void) {
    kad_refresh_buckets(NULL);
    printf("  test_refresh_buckets_null: OK\n");
}

static void test_same_node_bucket(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0);
    
    int idx = kad_get_bucket_index(&state, state.self_id);
    assert(idx == MAX_BUCKETS - 1);
    
    kad_cleanup(&state);
    printf("  test_same_node_bucket: OK\n");
}

static void test_update_existing_node(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0x0A000001);
    
    unsigned char node_id[NODE_ID_BYTES];
    memset(node_id, 1, NODE_ID_BYTES);
    
    int ret = kad_update_node(&state, node_id, 0x0A000002, 6881, 0);
    assert(ret >= 0);
    
    ret = kad_update_node(&state, node_id, 0x0A000003, 6882, 1);
    assert(ret == 0);
    
    kad_cleanup(&state);
    printf("  test_update_existing_node: OK\n");
}

static void test_bucket_full(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0);
    
    unsigned char base_id[NODE_ID_BYTES];
    memset(base_id, 0, NODE_ID_BYTES);
    base_id[0] = 0x80;
    
    for (int i = 0; i < K_BUCKET_SIZE + 5; i++) {
        unsigned char node_id[NODE_ID_BYTES];
        memcpy(node_id, base_id, NODE_ID_BYTES);
        node_id[NODE_ID_BYTES - 1] = (unsigned char)i;
        
        int ret = kad_update_node(&state, node_id, 0x0A000001 + (uint32_t)i, 6881, 0);
        if (i < K_BUCKET_SIZE) {
            assert(ret == 0);
        } else {
            assert(ret == -1);
        }
    }
    
    kad_cleanup(&state);
    printf("  test_bucket_full: OK\n");
}

static void test_refresh_old_buckets(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0);
    
    unsigned char node_id[NODE_ID_BYTES];
    memset(node_id, 1, NODE_ID_BYTES);
    kad_update_node(&state, node_id, 0x0A000001, 6881, 0);
    
    for (int i = 0; i < MAX_BUCKETS; i++) {
        if (state.buckets[i].count > 0) {
            state.buckets[i].last_update = time(NULL) - 1000;
        }
    }
    
    kad_refresh_buckets(&state);
    
    kad_cleanup(&state);
    printf("  test_refresh_old_buckets: OK\n");
}

static void test_refresh_fresh_buckets(void) {
    kad_state_t state;
    keypair_t kp;
    crypto_keypair_new(&kp);
    kad_init(&state, &kp, 0);

    unsigned char node_id[NODE_ID_BYTES];
    memset(node_id, 1, NODE_ID_BYTES);
    kad_update_node(&state, node_id, 0x0A000001, 6881, 0);

    /* last_update was just set to "now" by kad_update_node, so the
     * staleness condition (now - last_update > 900) is false: this
     * exercises the not-taken arm of that branch. */
    kad_refresh_buckets(&state);

    kad_cleanup(&state);
    printf("  test_refresh_fresh_buckets: OK\n");
}

int main(void) {
    if (crypto_init() < 0) {
        fprintf(stderr, "Failed to initialize crypto\n");
        return 1;
    }
    
    printf("test_kademlia:\n");
    
    test_init_cleanup();
    test_init_null();
    test_cleanup_null();
    test_update_node();
    test_update_node_null();
    test_get_bucket_index();
    test_get_bucket_index_null();
    test_routing_table_size();
    test_routing_table_size_null();
    test_refresh_buckets();
    test_refresh_buckets_null();
    test_same_node_bucket();
    test_update_existing_node();
    test_bucket_full();
    test_refresh_old_buckets();
    test_refresh_fresh_buckets();

    printf("test_kademlia: OK\n");
    return 0;
}