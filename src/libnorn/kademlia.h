/* SPDX-License-Identifier: MIT */
#ifndef KADEMLIA_H
#define KADEMLIA_H

#include <stdint.h>
#include <time.h>
#include "crypto.h"

#define K_BUCKET_SIZE 8
#define K_BITS 256
#define MAX_BUCKETS 256

typedef struct {
    unsigned char id[NODE_ID_BYTES];
    uint32_t ip;
    uint16_t port;
    uint8_t seed;
    time_t last_seen;
} kad_node_t;

typedef struct {
    kad_node_t nodes[K_BUCKET_SIZE];
    int count;
    time_t last_update;
} k_bucket_t;

typedef struct {
    unsigned char self_id[NODE_ID_BYTES];
    keypair_t *keypair;
    uint8_t self_seed;

    k_bucket_t buckets[MAX_BUCKETS];
    int bucket_count;

    time_t created;
} kad_state_t;

int kad_init(kad_state_t *state, keypair_t *kp, uint32_t external_ip);
void kad_cleanup(kad_state_t *state);

int kad_update_node(kad_state_t *state, const unsigned char *id, uint32_t ip, uint16_t port, uint8_t seed);


int kad_get_bucket_index(kad_state_t *state, const unsigned char *id);
void kad_refresh_buckets(kad_state_t *state);

size_t kad_routing_table_size(kad_state_t *state);

#endif