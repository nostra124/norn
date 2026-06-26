#include "kademlia.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int kad_init(kad_state_t *state, keypair_t *kp, uint32_t external_ip) {
    if (!state || !kp) return -1;
    
    memset(state, 0, sizeof(kad_state_t));
    state->keypair = kp;
    state->self_seed = 1;
    state->created = time(NULL);
    
    for (int i = 0; i < MAX_BUCKETS; i++) {
        state->buckets[i].count = 0;
        state->buckets[i].last_update = 0;
    }
    
    crypto_generate_node_id(state->self_id, external_ip, state->self_seed, kp->public_key);
    
    return 0;
}

void kad_cleanup(kad_state_t *state) {
    if (!state) return;
    /* nothing to free: the local key-value record store was removed (FEAT-010) */
    (void)state;
}

int kad_get_bucket_index(kad_state_t *state, const unsigned char *id) {
    if (!state || !id) return -1;
    
    unsigned char distance[NODE_ID_BYTES];
    crypto_xor_distance(state->self_id, id, distance, NODE_ID_BYTES);
    
    for (int i = 0; i < NODE_ID_BYTES; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            if ((distance[i] >> bit) & 1) {
                return i * 8 + (7 - bit);
            }
        }
    }
    
    return MAX_BUCKETS - 1;
}

int kad_update_node(kad_state_t *state, const unsigned char *id, uint32_t ip, uint16_t port, uint8_t seed) {
    if (!state || !id) return -1;
    
    int bucket_idx = kad_get_bucket_index(state, id);
    if (bucket_idx < 0 || bucket_idx >= MAX_BUCKETS) return -1;  /* LCOV_EXCL_BR_LINE: state/id already non-NULL, so kad_get_bucket_index always returns 0..MAX_BUCKETS-1 */
    
    k_bucket_t *bucket = &state->buckets[bucket_idx];
    
    for (int i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->nodes[i].id, id, NODE_ID_BYTES) == 0) {
            bucket->nodes[i].ip = ip;
            bucket->nodes[i].port = port;
            bucket->nodes[i].seed = seed;
            bucket->nodes[i].last_seen = time(NULL);
            bucket->last_update = time(NULL);
            return 0;
        }
    }
    
    if (bucket->count < K_BUCKET_SIZE) {
        memcpy(bucket->nodes[bucket->count].id, id, NODE_ID_BYTES);
        bucket->nodes[bucket->count].ip = ip;
        bucket->nodes[bucket->count].port = port;
        bucket->nodes[bucket->count].seed = seed;
        bucket->nodes[bucket->count].last_seen = time(NULL);
        bucket->count++;
        bucket->last_update = time(NULL);
        return 0;
    }
    
    return -1;
}

void kad_refresh_buckets(kad_state_t *state) {
    if (!state) return;
    
    time_t now = time(NULL);
    for (int i = 0; i < MAX_BUCKETS; i++) {
        if (state->buckets[i].count > 0) {
            if (now - state->buckets[i].last_update > 900) {
                state->buckets[i].last_update = now;
            }
        }
    }
}

size_t kad_routing_table_size(kad_state_t *state) {
    if (!state) return 0;
    
    size_t count = 0;
    for (int i = 0; i < MAX_BUCKETS; i++) {
        count += state->buckets[i].count;
    }
    return count;
}