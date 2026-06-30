/* SPDX-License-Identifier: MIT */
/**
 * @file recstore.h
 * @brief Trusted record store for signed items
 * 
 * Provides a shared store of signed records (BPE-0004). Unlike dhtstore
 * (untrusted network items), recstore holds records we signed ourselves.
 * All distribution paths (BEP-44 DHT put/get, gossip) funnel through this
 * single validating gate.
 * 
 * @par Key Features
 * - Trusted records (signed by us)
 * - Sequence monotonicity enforcement
 * - Account-based lookup (via node_id)
 * - Private record support (FEAT-048)
 * - Write-through persistence
 * 
 * @par Security Model
 * - Records are TRUSTED (signed by our own key)
 * - Sequence number must be newer than existing
 * - Signature verified before storage
 * - Private records never written to disk or DHT
 * 
 * @par Difference from dhtstore
 * - dhtstore: Untrusted items from the network (bounded cache)
 * - recstore: Trusted items we signed ourselves (persistent store)
 * 
 * @note Not thread-safe. Caller must synchronize.
 */

#ifndef NORN_RECSTORE_H
#define NORN_RECSTORE_H

#include <stdint.h>
#include <stddef.h>

/** @brief Maximum number of records */
#define RECSTORE_MAX  256

/** @brief Maximum record value size */
#define RECSTORE_VMAX 256

/**
 * @brief Trusted record structure
 */
typedef struct {
    unsigned char target[20];   /**< SHA1(k) - DHT key */
    unsigned char k[32];         /**< Publisher Ed25519 identity pubkey */
    uint32_t      seq;          /**< Monotonically increasing sequence number */
    unsigned char v[RECSTORE_VMAX]; /**< Record value */
    size_t        vlen;         /**< Value length */
    unsigned char sig[64];      /**< Ed25519 signature over BEP-44 canonical buffer */
    long          last_seen;    /**< Unix timestamp when last seen */
    char          via[64];      /**< Account of peer that forwarded this record */
    int           priv;         /**< FEAT-048: 1 if private, 0 if public */
} rec_t;

/**
 * @brief Initialize the record store
 * 
 * Loads existing records from disk (if path exists) and enables write-through
 * persistence. Subsequent changes are automatically saved to disk.
 * 
 * @param path File path for persistence (NULL for in-memory only)
 * @return Number of records loaded, or -1 on error
 * 
 * @note Thread Safety: Not thread-safe
 * @note Persistence: Records saved automatically after each change
 * @note Memory: Maximum RECSTORE_MAX records
 * 
 * @code
 * // Persistent storage
 * int count = recstore_init("/var/lib/myapp/records.dat");
 * if (count < 0) {
 *     fprintf(stderr, "Failed to init recstore\n");
 *     return 1;
 * }
 * 
 * // In-memory only
 * recstore_init(NULL);
 * @endcode
 */
int recstore_init(const char *path);

/**
 * @brief Accept and store a trusted record
 * 
 * Validates the signature against k, verifies SHA1(k) == target, and checks
 * that seq is strictly newer than any existing record for this target.
 * 
 * @param k Ed25519 public key (32 bytes)
 * @param seq Sequence number (must be newer than existing)
 * @param v Value (max RECSTORE_VMAX bytes)
 * @param vlen Length of value
 * @param sig Ed25519 signature (64 bytes)
 * @return 1 if accepted (new or newer), 0 if rejected (bad sig, stale, duplicate, full)
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if k, v, or sig is NULL
 * @note Security: Signature verified, sequence monotonicity enforced
 * @note Persistence: Automatically saved if initialized with path
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
 * if (recstore_accept(kp.public_key, 1, value, sizeof(value)-1, sig)) {
 *     printf("Accepted\n");
 * }
 * @endcode
 */
int recstore_accept(const unsigned char k[32], uint32_t seq,
                    const unsigned char *v, size_t vlen, const unsigned char sig[64]);

/**
 * @brief Retrieve a record by target
 * 
 * Fetches a previously stored record by its target (SHA1(k)).
 * 
 * @param target DHT key (20 bytes)
 * @param out Output: record structure
 * @return 1 if found, 0 if not found
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if target or out is NULL
 */
int recstore_get(const unsigned char target[20], rec_t *out);

/**
 * @brief Retrieve a record by public key
 * 
 * Fetches a previously stored record by its Ed25519 public key.
 * The target is computed as SHA1(k).
 * 
 * @param k Ed25519 public key (32 bytes)
 * @param out Output: record structure
 * @return 1 if found, 0 if not found
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if k or out is NULL
 */
int recstore_get_by_pubkey(const unsigned char k[32], rec_t *out);

/**
 * @brief Retrieve a record by node ID
 * 
 * Fetches a record by its node_id field (SHA256(account)[:20]).
 * This enables account-based lookup without sharing the account itself.
 * 
 * @param node_id SHA256(account)[:20] (20 bytes)
 * @param out Output: record structure
 * @return 1 if found, 0 if not found
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if node_id or out is NULL
 * @note FEAT-048: One-way hash reveals nothing about account
 */
int recstore_get_by_node_id(const unsigned char node_id[20], rec_t *out);

/**
 * @brief Set the "via" field for a record
 * 
 * Records the account of the peer that forwarded this record to us.
 * Used for peer tracking and debugging.
 * 
 * @param k Ed25519 public key (32 bytes)
 * @param via Account string (peer identifier)
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Does nothing if k or via is NULL
 * @note No-op: If record not held
 */
void recstore_set_via(const unsigned char k[32], const char *via);

/**
 * @brief Set the private flag for a record
 * 
 * Marks a record as private (FEAT-048). Private records are:
 * - Kept in memory for resolution
 * - Never written to disk
 * - Never published to mainline DHT
 * - Never gossiped to non-trusted peers
 * 
 * @param k Ed25519 public key (32 bytes)
 * @param val 1 for private, 0 for public
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Does nothing if k is NULL
 * @note No-op: If record not held
 * @note Memory-only: Private flag not persisted
 */
void recstore_set_private(const unsigned char k[32], int val);

/**
 * @brief Get number of records stored
 * @return Number of records currently held
 */
int recstore_count(void);

/**
 * @brief List all records
 * 
 * Enumerates all records for gossip digests.
 * 
 * @param out Output array
 * @param max Maximum number of records to return
 * @return Number of records returned
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns 0 if out is NULL
 */
int recstore_list(rec_t *out, int max);

#endif /* NORN_RECSTORE_H */