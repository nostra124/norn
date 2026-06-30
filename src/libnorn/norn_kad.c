/* SPDX-License-Identifier: MIT */
/**
 * @file norn_kad.c
 * @brief Parameterised Kademlia routing table implementation.
 */

#include "norn_kad.h"
#include <stdlib.h>
#include <string.h>

kad_table_t *kad_table_new(const norn_crypto_suite_t *suite, const unsigned char *self_id) {
    if (!suite || !self_id) return NULL;
    
    size_t nodeid_len = suite->nodeid_len;
    if (nodeid_len == 0) return NULL;
    
    kad_table_t *table = calloc(1, sizeof(kad_table_t));
    if (!table) return NULL;
    
    table->suite = suite;
    table->nodeid_len = nodeid_len;
    table->k = KAD_K_DEFAULT;
    table->bucket_count = nodeid_len * 8;  /* One bucket per bit */
    
    table->self_id = malloc(nodeid_len);
    if (!table->self_id) {
        free(table);
        return NULL;
    }
    memcpy(table->self_id, self_id, nodeid_len);
    
    table->buckets = calloc(table->bucket_count, sizeof(k_bucket_t));
    if (!table->buckets) {
        free(table->self_id);
        free(table);
        return NULL;
    }
    
    table->created = time(NULL);
    return table;
}

void kad_table_free(kad_table_t *table) {
    if (!table) return;
    
    if (table->buckets) {
        for (int i = 0; i < table->bucket_count; i++) {
            k_bucket_t *bucket = &table->buckets[i];
            if (bucket->nodes) {
                for (int j = 0; j < bucket->count; j++) {
                    free(bucket->nodes[j].id);
                }
                free(bucket->nodes);
            }
        }
        free(table->buckets);
    }
    
    free(table->self_id);
    free(table);
}

int kad_table_bucket_index(const kad_table_t *table, const unsigned char *id) {
    if (!table || !id) return -1;
    
    /* Calculate XOR distance */
    unsigned char *distance = malloc(table->nodeid_len);
    if (!distance) return -1;
    
    kad_table_xor_distance(table, table->self_id, id, distance);
    
    /* Find the first 1-bit (bucket index) */
    for (size_t i = 0; i < table->nodeid_len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            if ((distance[i] >> bit) & 1) {
                free(distance);
                return (int)(i * 8 + (7 - bit));
            }
        }
    }
    
    /* All zeros = our own ID */
    free(distance);
    return table->bucket_count - 1;
}

int kad_table_xor_distance(const kad_table_t *table, const unsigned char *a,
                           const unsigned char *b, unsigned char *result) {
    if (!table || !a || !b || !result) return -1;
    
    for (size_t i = 0; i < table->nodeid_len; i++) {
        result[i] = a[i] ^ b[i];
    }
    return 0;
}

int kad_table_compare_distance(const kad_table_t *table, const unsigned char *a,
                               const unsigned char *b) {
    if (!table) return 0;
    
    for (size_t i = 0; i < table->nodeid_len; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

int kad_table_insert(kad_table_t *table, const unsigned char *id,
                     uint32_t ip, uint16_t port, uint8_t seed) {
    if (!table || !id) return -1;
    
    int bucket_idx = kad_table_bucket_index(table, id);
    if (bucket_idx < 0 || bucket_idx >= table->bucket_count) return -1;
    
    k_bucket_t *bucket = &table->buckets[bucket_idx];
    
    /* Check if node already exists */
    for (int i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->nodes[i].id, id, table->nodeid_len) == 0) {
            /* Update existing node */
            bucket->nodes[i].ip = ip;
            bucket->nodes[i].port = port;
            bucket->nodes[i].seed = seed;
            bucket->nodes[i].last_seen = time(NULL);
            bucket->last_update = time(NULL);
            return 0;
        }
    }
    
    /* Add new node */
    if (bucket->count >= bucket->capacity) {
        int new_capacity = bucket->capacity == 0 ? table->k : bucket->capacity * 2;
        kad_node_t *new_nodes = realloc(bucket->nodes, new_capacity * sizeof(kad_node_t));
        if (!new_nodes) return -1;
        bucket->nodes = new_nodes;
        bucket->capacity = new_capacity;
    }
    
    kad_node_t *node = &bucket->nodes[bucket->count];
    node->id = malloc(table->nodeid_len);
    if (!node->id) return -1;
    
    memcpy(node->id, id, table->nodeid_len);
    node->ip = ip;
    node->port = port;
    node->seed = seed;
    node->last_seen = time(NULL);
    node->bucket_idx = bucket_idx;
    
    bucket->count++;
    bucket->last_update = time(NULL);
    
    return 0;
}

int kad_table_remove(kad_table_t *table, const unsigned char *id) {
    if (!table || !id) return -1;
    
    int bucket_idx = kad_table_bucket_index(table, id);
    if (bucket_idx < 0 || bucket_idx >= table->bucket_count) return -1;
    
    k_bucket_t *bucket = &table->buckets[bucket_idx];
    
    for (int i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->nodes[i].id, id, table->nodeid_len) == 0) {
            free(bucket->nodes[i].id);
            
            /* Shift remaining nodes */
            for (int j = i; j < bucket->count - 1; j++) {
                bucket->nodes[j] = bucket->nodes[j + 1];
            }
            bucket->count--;
            
            return 0;
        }
    }
    
    return -1;  /* Not found */
}

int kad_count_leading_zeros(const unsigned char *id, size_t len) {
    int count = 0;
    
    for (size_t i = 0; i < len; i++) {
        if (id[i] == 0) {
            count += 8;
        } else {
            for (int bit = 7; bit >= 0; bit--) {
                if ((id[i] >> bit) & 1) {
                    return count;
                }
                count++;
            }
        }
    }
    
    return count;
}

int kad_table_closest(const kad_table_t *table, const unsigned char *target,
                      kad_node_t **nodes, int k) {
    if (!table || !target || !nodes || k <= 0) return -1;
    
    /* Collect all nodes */
    int total = 0;
    for (int i = 0; i < table->bucket_count; i++) {
        total += table->buckets[i].count;
    }
    
    if (total == 0) return 0;
    
    /* Create array of all nodes */
    kad_node_t **all_nodes = malloc(total * sizeof(kad_node_t *));
    if (!all_nodes) return -1;
    
    int idx = 0;
    for (int i = 0; i < table->bucket_count; i++) {
        k_bucket_t *bucket = &table->buckets[i];
        for (int j = 0; j < bucket->count; j++) {
            all_nodes[idx++] = &bucket->nodes[j];
        }
    }
    
    /* Sort by distance to target */
    /* Simple insertion sort for now - could use qsort with context */
    for (int i = 1; i < total; i++) {
        kad_node_t *key = all_nodes[i];
        int j = i - 1;
        
        unsigned char dist_key[264], dist_j[264];
        kad_table_xor_distance(table, target, key->id, dist_key);
        
        while (j >= 0) {
            kad_table_xor_distance(table, target, all_nodes[j]->id, dist_j);
            if (kad_table_compare_distance(table, dist_key, dist_j) >= 0) break;
            all_nodes[j + 1] = all_nodes[j];
            j--;
        }
        all_nodes[j + 1] = key;
    }
    
    /* Return k closest */
    int result_count = (total < k) ? total : k;
    for (int i = 0; i < result_count; i++) {
        nodes[i] = all_nodes[i];
    }
    
    free(all_nodes);
    return result_count;
}

size_t kad_table_size(const kad_table_t *table) {
    if (!table) return 0;
    
    size_t count = 0;
    for (int i = 0; i < table->bucket_count; i++) {
        count += table->buckets[i].count;
    }
    return count;
}

void kad_table_refresh(kad_table_t *table) {
    if (!table) return;
    
    time_t now = time(NULL);
    for (int i = 0; i < table->bucket_count; i++) {
        if (table->buckets[i].count > 0) {
            if (now - table->buckets[i].last_update > 900) {  /* 15 minutes */
                table->buckets[i].last_update = now;
            }
        }
    }
}