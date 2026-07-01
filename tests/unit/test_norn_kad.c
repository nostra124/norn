/* SPDX-License-Identifier: MIT */
/**
 * @file test_norn_kad.c
 * @brief Unit tests for parameterised Kademlia routing table (FEAT-014)
 */

#include "norn_suite.h"
#include "norn_kad.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_kad_table_new_free(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char self_id[32];
    memset(self_id, 0xAB, 32);
    
    kad_table_t *table = kad_table_new(suite, self_id);
    assert(table != NULL);
    assert(table->suite == suite);
    assert(table->nodeid_len == 32);
    assert(table->bucket_count == 256);  /* 32 * 8 */
    assert(table->k == KAD_K_DEFAULT);
    assert(memcmp(table->self_id, self_id, 32) == 0);
    
    kad_table_free(table);
    
    /* NULL inputs */
    table = kad_table_new(NULL, self_id);
    assert(table == NULL);
    table = kad_table_new(suite, NULL);
    assert(table == NULL);
    
    printf("  test_kad_table_new_free: OK\n");
}

static void test_kad_table_bucket_index(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char self_id[32];
    memset(self_id, 0, 32);  /* All zeros */
    
    kad_table_t *table = kad_table_new(suite, self_id);
    assert(table != NULL);
    
    /* Node with first bit set should go to bucket 0 */
    unsigned char id[32];
    memset(id, 0, 32);
    id[0] = 0x80;  /* 1000 0000 - first bit set */
    int bucket = kad_table_bucket_index(table, id);
    assert(bucket == 0);
    
    /* Node with second bit set should go to bucket 1 */
    memset(id, 0, 32);
    id[0] = 0x40;  /* 0100 0000 - second bit set */
    bucket = kad_table_bucket_index(table, id);
    assert(bucket == 1);
    
    /* Node with last bit set */
    memset(id, 0, 32);
    id[31] = 0x01;  /* Last bit set */
    bucket = kad_table_bucket_index(table, id);
    assert(bucket == 255);
    
    /* Same as self (all zeros) = last bucket */
    memset(id, 0, 32);
    bucket = kad_table_bucket_index(table, id);
    assert(bucket == 255);
    
    /* NULL inputs */
    bucket = kad_table_bucket_index(NULL, id);
    assert(bucket == -1);
    bucket = kad_table_bucket_index(table, NULL);
    assert(bucket == -1);
    
    kad_table_free(table);
    
    printf("  test_kad_table_bucket_index: OK\n");
}

static void test_kad_table_insert_remove(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char self_id[32];
    memset(self_id, 0, 32);
    
    kad_table_t *table = kad_table_new(suite, self_id);
    assert(table != NULL);
    
    /* Insert node */
    unsigned char id[32];
    memset(id, 0x80, 32);  /* First bit set */
    int ret = kad_table_insert(table, id, 0x01020304, 6881, 1);
    assert(ret == 0);
    assert(kad_table_size(table) == 1);
    
    /* Insert same node again (update) */
    ret = kad_table_insert(table, id, 0x05060708, 6882, 2);
    assert(ret == 0);
    assert(kad_table_size(table) == 1);  /* Still 1, not 2 */
    
    /* Insert different node */
    unsigned char id2[32];
    memset(id2, 0x40, 32);  /* Second bit set */
    ret = kad_table_insert(table, id2, 0x090A0B0C, 6883, 3);
    assert(ret == 0);
    assert(kad_table_size(table) == 2);
    
    /* Remove first node */
    ret = kad_table_remove(table, id);
    assert(ret == 0);
    assert(kad_table_size(table) == 1);
    
    /* Remove again (not found) */
    ret = kad_table_remove(table, id);
    assert(ret == -1);
    assert(kad_table_size(table) == 1);
    
    /* NULL inputs */
    ret = kad_table_insert(NULL, id, 0, 0, 0);
    assert(ret == -1);
    ret = kad_table_insert(table, NULL, 0, 0, 0);
    assert(ret == -1);
    ret = kad_table_remove(NULL, id);
    assert(ret == -1);
    ret = kad_table_remove(table, NULL);
    assert(ret == -1);
    
    kad_table_free(table);
    
    printf("  test_kad_table_insert_remove: OK\n");
}

static void test_kad_table_closest(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char self_id[32];
    memset(self_id, 0, 32);
    
    kad_table_t *table = kad_table_new(suite, self_id);
    assert(table != NULL);
    
    /* Insert 10 nodes */
    for (int i = 0; i < 10; i++) {
        unsigned char id[32];
        memset(id, 0, 32);
        id[0] = (i + 1) << 4;  /* Different buckets */
        kad_table_insert(table, id, i, 6881 + i, i);
    }
    assert(kad_table_size(table) == 10);
    
    /* Find 5 closest */
    unsigned char target[32];
    memset(target, 0x10, 32);
    
    kad_node_t *nodes[10];
    int count = kad_table_closest(table, target, nodes, 5);
    assert(count == 5);
    
    /* Verify sorted by distance */
    unsigned char prev_dist[264];
    memset(prev_dist, 0, sizeof(prev_dist));
    
    for (int i = 0; i < count; i++) {
        unsigned char dist[264];
        kad_table_xor_distance(table, target, nodes[i]->id, dist);
        
        /* Should be sorted: each distance >= previous */
        if (i > 0) {
            assert(kad_table_compare_distance(table, prev_dist, dist) <= 0);
        }
        memcpy(prev_dist, dist, table->nodeid_len);
    }
    
    /* Find all */
    count = kad_table_closest(table, target, nodes, 10);
    assert(count == 10);
    
    /* Empty table */
    kad_table_t *empty = kad_table_new(suite, self_id);
    assert(empty != NULL);
    count = kad_table_closest(empty, target, nodes, 5);
    assert(count == 0);
    kad_table_free(empty);
    
    /* NULL inputs */
    count = kad_table_closest(NULL, target, nodes, 5);
    assert(count == -1);
    count = kad_table_closest(table, NULL, nodes, 5);
    assert(count == -1);
    count = kad_table_closest(table, target, NULL, 5);
    assert(count == -1);
    
    kad_table_free(table);
    
    printf("  test_kad_table_closest: OK\n");
}

static void test_kad_table_xor_distance(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char self_id[32];
    memset(self_id, 0, 32);
    
    kad_table_t *table = kad_table_new(suite, self_id);
    assert(table != NULL);
    
    unsigned char a[32], b[32], result[32];
    memset(a, 0xFF, 32);
    memset(b, 0x00, 32);
    
    int ret = kad_table_xor_distance(table, a, b, result);
    assert(ret == 0);
    
    /* XOR of 0xFF and 0x00 should be 0xFF */
    for (int i = 0; i < 32; i++) {
        assert(result[i] == 0xFF);
    }
    
    /* XOR of same values should be all zeros */
    ret = kad_table_xor_distance(table, a, a, result);
    assert(ret == 0);
    for (int i = 0; i < 32; i++) {
        assert(result[i] == 0);
    }
    
    /* NULL inputs */
    ret = kad_table_xor_distance(NULL, a, b, result);
    assert(ret == -1);
    ret = kad_table_xor_distance(table, NULL, b, result);
    assert(ret == -1);
    ret = kad_table_xor_distance(table, a, NULL, result);
    assert(ret == -1);
    ret = kad_table_xor_distance(table, a, b, NULL);
    assert(ret == -1);
    
    kad_table_free(table);
    
    printf("  test_kad_table_xor_distance: OK\n");
}

static void test_kad_count_leading_zeros(void) {
    /* All zeros = 256 leading zeros (32 bytes * 8) */
    unsigned char id[32];
    memset(id, 0, 32);
    int zeros = kad_count_leading_zeros(id, 32);
    assert(zeros == 256);
    
    /* First bit set = 0 leading zeros */
    id[0] = 0x80;
    zeros = kad_count_leading_zeros(id, 32);
    assert(zeros == 0);
    
    /* Second bit set = 1 leading zero */
    id[0] = 0x40;
    zeros = kad_count_leading_zeros(id, 32);
    assert(zeros == 1);
    
    /* First byte 0, second byte has first bit set = 8 leading zeros */
    id[0] = 0;
    id[1] = 0x80;
    zeros = kad_count_leading_zeros(id, 32);
    assert(zeros == 8);
    
    printf("  test_kad_count_leading_zeros: OK\n");
}

static void test_kad_table_different_id_widths(void) {
    /* Test that the table works with different ID widths */
    /* We can't test secp256k1 without implementing that suite,
     * but we can verify the parameterization works */
    
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    assert(suite->nodeid_len == 32);
    
    unsigned char self_id[32];
    memset(self_id, 0, 32);
    
    kad_table_t *table = kad_table_new(suite, self_id);
    assert(table != NULL);
    assert(table->nodeid_len == 32);
    assert(table->bucket_count == 256);  /* 32 * 8 */
    
    kad_table_free(table);
    
    printf("  test_kad_table_different_id_widths: OK\n");
}

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    printf("test_norn_kad:\n");
    
    test_kad_table_new_free();
    test_kad_table_bucket_index();
    test_kad_table_insert_remove();
    test_kad_table_closest();
    test_kad_table_xor_distance();
    test_kad_count_leading_zeros();
    test_kad_table_different_id_widths();
    
    printf("test_norn_kad: OK\n");
    return 0;
}