/* SPDX-License-Identifier: MIT */
/**
 * @file dhtstore.h
 * @brief Bounded cache for untrusted DHT items
 * 
 * Provides volatile, capped storage for BEP-44 mutable and immutable items
 * that this node holds for the network as a good DHT citizen (BPE-0004).
 * 
 * @par Key Features
 * - Byte budget (RAM/512 by default, max 64MB)
 * - Per-source-IP rate limiting (max 32 items/IP)
 * - 2-hour TTL with LRU eviction
 * - Signature verification before storage
 * - Sequence number monotonicity enforcement
 * 
 * @par Security Model
 * - Items are UNTRUSTED (from arbitrary DHT nodes)
 * - Signature verified before storage
 * - Sequence number must be newer than existing
 * - Per-IP rate limiting prevents abuse
 * - Budget enforcement prevents memory exhaustion
 * 
 * @par Difference from recstore
 * - dhtstore: Untrusted items from the network
 * - recstore: Trusted items we signed ourselves
 * 
 * @note Not thread-safe. Caller must synchronize.
 */

#ifndef NORN_DHTSTORE_H
#define NORN_DHTSTORE_H

#include <stdint.h>
#include <stddef.h>

/** @brief BEP-44 max value size (1000 bytes) */
#define DHTSTORE_VMAX     1000

/** @brief BEP-44 TTL (2 hours in seconds) */
#define DHTSTORE_TTL      7200

/** @brief Max items per source IP (rate limiting) */
#define DHTSTORE_PER_IP   32

/**
 * @brief Initialize the DHT store with a byte budget
 * 
 * Sets the maximum memory usage for untrusted DHT items. Items exceeding
 * the budget trigger LRU eviction of oldest/expired items.
 * 
 * @param budget_mb Budget in megabytes, or:
 *                  - <0: Auto-detect from RAM (RAM/512, clamped to 2-64MB)
 *                  - 0: Client-only mode (no storage)
 * @param client_only 1 for client-only mode (no storage), 0 for normal operation
 * @return Effective budget in bytes, or 0 for client-only mode
 * 
 * @note Thread Safety: Not thread-safe
 * @note Side Effects: Clears existing store
 * @note Memory: Budget includes item metadata (~100 bytes/item)
 * 
 * @code
 * // Auto-detect budget (RAM/512)
 * size_t budget = dhtstore_init(-1, 0);
 * printf("Budget: %zu MB\n", budget / (1024 * 1024));
 * 
 * // Fixed budget: 10 MB
 * dhtstore_init(10, 0);
 * 
 * // Client-only mode (no storage)
 * dhtstore_init(0, 1);
 * @endcode
 */
size_t dhtstore_init(int budget_mb, int client_only);

/**
 * @brief Store a mutable signed item (BEP-44)
 * 
 * Validates and stores a mutable item. Verifies the signature, checks
 * sequence monotonicity, and enforces per-IP rate limits and byte budget.
 * 
 * @param target DHT key (SHA1(k || salt), 20 bytes)
 * @param k Ed25519 public key (32 bytes)
 * @param seq Sequence number (must be newer than existing)
 * @param v Value (max DHTSTORE_VMAX bytes)
 * @param vlen Length of value
 * @param sig Ed25519 signature (64 bytes)
 * @param salt Salt value (NULL for unsalted items)
 * @param saltlen Length of salt
 * @param src_ip Source IP (network byte order) for rate limiting
 * @return 1 if stored, 0 if rejected (bad sig, stale seq, rate limited, budget exceeded)
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if target, k, v, or sig is NULL
 * @note Security: Signature verified, seq monotonicity enforced
 * @note LRU: May evict oldest items to make room
 * 
 * @code
 * keypair_t kp;
 * crypto_keypair_new(&kp);
 * 
 * unsigned char target[20];
 * bep44_target(kp.public_key, target);
 * 
 * unsigned char value[] = "Hello";
 * unsigned char buf[300], sig[64];
 * int len = bep44_signbuf(1, value, sizeof(value)-1, buf, sizeof(buf));
 * bf_sign(sig, buf, len, kp.secret_key);
 * 
 * if (dhtstore_put(target, kp.public_key, 1, value, sizeof(value)-1, sig, NULL, 0, src_ip)) {
 *     printf("Stored\n");
 * }
 * @endcode
 */
int dhtstore_put(const unsigned char target[20], const unsigned char k[32],
                 uint32_t seq, const unsigned char *v, size_t vlen,
                 const unsigned char sig[64],
                 const unsigned char *salt, size_t saltlen, uint32_t src_ip);

/**
 * @brief Store an immutable item (BEP-44)
 * 
 * Stores a content-addressed item. The key is SHA1(bencode(v)), computed
 * automatically. Immutable items are self-verifying (no signature needed).
 * 
 * @param v Value (max DHTSTORE_VMAX bytes)
 * @param vlen Length of value
 * @param src_ip Source IP for rate limiting
 * @param target_out Output: computed target (SHA1 hash, 20 bytes)
 * @return 1 if stored or already held, 0 if rejected
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if v or target_out is NULL
 * @note Content-addressed: Identical values have same target
 * @note BEP-44: Values > 1000 bytes rejected
 */
int dhtstore_put_immutable(const unsigned char *v, size_t vlen, uint32_t src_ip,
                           unsigned char target_out[20]);

/**
 * @brief Retrieve an item by target
 * 
 * Fetches a previously stored item. Items are automatically expired after
 * DHTSTORE_TTL seconds.
 * 
 * @param target DHT key (20 bytes)
 * @param k_out Output: Ed25519 public key (32 bytes, or NULL)
 * @param seq_out Output: sequence number (or NULL)
 * @param v_out Output: value buffer
 * @param vcap Capacity of v_out
 * @param vlen_out Output: actual value length (or NULL)
 * @param sig_out Output: Ed25519 signature (64 bytes, or NULL)
 * @return 1 if found, 0 if not found or expired
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if target is NULL
 * @note Ownership: All outputs are caller-owned
 * @note Truncation: If v_out is too small, value is truncated to vcap bytes
 */
int dhtstore_get(const unsigned char target[20], unsigned char k_out[32],
                 uint32_t *seq_out, unsigned char *v_out, size_t vcap,
                 size_t *vlen_out, unsigned char sig_out[64]);

/**
 * @brief Retrieve an item with immutable flag
 * 
 * Same as dhtstore_get(), plus returns whether the item is immutable.
 * 
 * @param target DHT key (20 bytes)
 * @param k_out Output: Ed25519 public key (32 bytes, or NULL)
 * @param seq_out Output: sequence number (or NULL)
 * @param v_out Output: value buffer
 * @param vcap Capacity of v_out
 * @param vlen_out Output: actual value length (or NULL)
 * @param sig_out Output: signature (64 bytes, or NULL)
 * @param immutable_out Output: 1 if immutable, 0 if mutable (or NULL)
 * @return 1 if found, 0 if not found
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if target is NULL
 */
int dhtstore_get_ex(const unsigned char target[20], unsigned char k_out[32],
                    uint32_t *seq_out, unsigned char *v_out, size_t vcap,
                    size_t *vlen_out, unsigned char sig_out[64], int *immutable_out);

/**
 * @brief Get current bytes stored
 * @return Total bytes currently in use
 */
size_t dhtstore_bytes(void);

/**
 * @brief Get number of items stored
 * @return Number of items currently held
 */
int dhtstore_count(void);

/**
 * @brief Remove an item by target
 * 
 * Removes the item at the given target. This is used for BUG-122 cleanup.
 * 
 * @param target DHT key (20 bytes)
 * @return 1 if removed, 0 if not found
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if target is NULL
 */
int dhtstore_del(const unsigned char target[20]);

/**
 * @brief Item information for enumeration
 */
typedef struct {
    unsigned char target[20];  /**< DHT key */
    int           immutable;   /**< 1 if immutable, 0 if mutable */
    size_t        vlen;        /**< Value length */
    uint32_t      seq;         /**< Sequence number (mutable only) */
    long          stored;      /**< Unix timestamp when stored */
} dht_item_info_t;

/**
 * @brief Enumerate held items
 * 
 * Lists items of one kind (mutable or immutable) for enumeration.
 * 
 * @param want_immutable 1 for immutable items, 0 for mutable items
 * @param out Output array
 * @param max Maximum number of items to return
 * @return Number of items returned
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if out is NULL
 */
int dhtstore_list(int want_immutable, dht_item_info_t *out, int max);

#endif /* NORN_DHTSTORE_H */