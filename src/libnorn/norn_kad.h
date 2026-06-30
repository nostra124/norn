/* SPDX-License-Identifier: MIT */
/**
 * @file norn_kad.h
 * @brief Parameterised Kademlia routing table.
 *
 * This is the native overlay Kademlia implementation with configurable
 * node ID width. The public mainline DHT (BEP-5/44) stays fixed at
 * 20-byte IDs and uses a separate implementation.
 *
 * @par Usage
 *
 * The routing table is parameterized on node ID length from the crypto suite:
 * - Ed25519/SHA-256: 32-byte IDs (norn_suite_sodium)
 * - secp256k1: 33-byte IDs (compressed public key as ID)
 * - Custom: any length supported
 *
 * @code
 * kad_table_t *table = kad_table_new(suite);
 * kad_table_insert(table, node_id, ip, port);
 * kad_table_closest(table, target, nodes, k);
 * kad_table_free(table);
 * @endcode
 */

#ifndef NORN_KAD_H
#define NORN_KAD_H

#include "norn_suite.h"
#include <stdint.h>
#include <stddef.h>
#include <time.h>

/** @brief Default k-bucket size (Kademlia constant) */
#define KAD_K_DEFAULT 8

/** @brief Maximum number of buckets (8 * max_node_id_bytes) */
#define KAD_MAX_BUCKETS 264  /* 8 * 33 bytes (secp256k1 compressed) */

/**
 * @brief Node in the routing table.
 */
typedef struct {
    unsigned char *id;     /**< Node ID (nodeid_len bytes, allocated) */
    uint32_t ip;           /**< IP address (network byte order) */
    uint16_t port;         /**< Port (network byte order) */
    uint8_t seed;          /**< BEP-42 seed */
    time_t last_seen;      /**< Last contact time */
    int bucket_idx;        /**< Bucket index (-1 if not in table) */
} kad_node_t;

/**
 * @brief k-bucket in the routing table.
 */
typedef struct {
    kad_node_t *nodes;     /**< Nodes in this bucket */
    int count;             /**< Number of nodes */
    int capacity;          /**< Allocated capacity */
    time_t last_update;    /**< Last bucket update */
} k_bucket_t;

/**
 * @brief Kademlia routing table.
 */
typedef struct {
    const norn_crypto_suite_t *suite;  /**< Crypto suite (determines ID length) */
    unsigned char *self_id;             /**< Our node ID (nodeid_len bytes) */
    size_t nodeid_len;                  /**< Node ID length in bytes */
    int k;                              /**< Bucket size (default KAD_K_DEFAULT) */
    
    k_bucket_t *buckets;                /**< Bucket array (8 * nodeid_len buckets) */
    int bucket_count;                   /**< Number of buckets */
    
    time_t created;                     /**< Table creation time */
} kad_table_t;

/**
 * @brief Create a new routing table.
 *
 * @param suite Crypto suite (determines node ID length)
 * @param self_id Our node ID (nodeid_len bytes, copied)
 * @return New routing table, or NULL on error
 */
kad_table_t *kad_table_new(const norn_crypto_suite_t *suite, const unsigned char *self_id);

/**
 * @brief Free a routing table.
 */
void kad_table_free(kad_table_t *table);

/**
 * @brief Insert or update a node in the routing table.
 *
 * @param table Routing table
 * @param id Node ID (nodeid_len bytes)
 * @param ip IP address (network byte order)
 * @param port Port (network byte order)
 * @param seed BEP-42 seed
 * @return 0 on success, -1 on error
 */
int kad_table_insert(kad_table_t *table, const unsigned char *id,
                     uint32_t ip, uint16_t port, uint8_t seed);

/**
 * @brief Remove a node from the routing table.
 *
 * @param table Routing table
 * @param id Node ID (nodeid_len bytes)
 * @return 0 on success, -1 if not found
 */
int kad_table_remove(kad_table_t *table, const unsigned char *id);

/**
 * @brief Find the k closest nodes to a target.
 *
 * @param table Routing table
 * @param target Target ID (nodeid_len bytes)
 * @param nodes Output array for closest nodes
 * @param k Maximum number of nodes to return
 * @return Number of nodes found, or -1 on error
 */
int kad_table_closest(const kad_table_t *table, const unsigned char *target,
                      kad_node_t **nodes, int k);

/**
 * @brief Get the bucket index for a node ID.
 *
 * @param table Routing table
 * @param id Node ID (nodeid_len bytes)
 * @return Bucket index (0 to 8*nodeid_len-1), or -1 on error
 */
int kad_table_bucket_index(const kad_table_t *table, const unsigned char *id);

/**
 * @brief Calculate XOR distance between two node IDs.
 *
 * @param table Routing table (for nodeid_len)
 * @param a First node ID
 * @param b Second node ID
 * @param result Output buffer for XOR distance (nodeid_len bytes)
 * @return 0 on success, -1 on error
 */
int kad_table_xor_distance(const kad_table_t *table, const unsigned char *a,
                           const unsigned char *b, unsigned char *result);

/**
 * @brief Compare two XOR distances (for sorting).
 *
 * Returns <0 if a<b, >0 if a>b, 0 if equal.
 * Used to find closest nodes to a target.
 *
 * @param table Routing table (for nodeid_len)
 * @param a First distance
 * @param b Second distance
 * @return Comparison result
 */
int kad_table_compare_distance(const kad_table_t *table, const unsigned char *a,
                               const unsigned char *b);

/**
 * @brief Get the total number of nodes in the routing table.
 */
size_t kad_table_size(const kad_table_t *table);

/**
 * @brief Refresh stale buckets.
 *
 * Buckets not updated in 15 minutes are marked stale.
 *
 * @param table Routing table
 */
void kad_table_refresh(kad_table_t *table);

/**
 * @brief Count leading zeros in a node ID.
 *
 * Used for BEP-42 node ID generation and bucket placement.
 *
 * @param id Node ID
 * @param len Length in bytes
 * @return Number of leading zero bits
 */
int kad_count_leading_zeros(const unsigned char *id, size_t len);

#endif /* NORN_KAD_H */