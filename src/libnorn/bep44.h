/**
 * @file bep44.h
 * @brief BEP-44 mutable and immutable items for DHT
 * 
 * Implements BEP-44: Mutable and immutable items for BitTorrent DHT.
 * Mutable items are signed with Ed25519 keys and can be updated by the
 * key holder. Immutable items are content-addressed (SHA1 hash).
 * 
 * @par Mutable Items
 * - target = SHA1("k" || pubkey)
 * - Value signed with Ed25519 secret key
 * - Sequence number must increase monotonically
 * - Max value size: 1000 bytes
 * 
 * @par Immutable Items
 * - target = SHA1(bencode(value))
 * - Content-addressed, self-verifying
 * - No signature or sequence number
 * - Max value size: 1000 bytes
 * 
 * @par Security
 * - Ed25519 signatures prevent forgery
 * - Sequence numbers prevent replay attacks
 * - SHA1 hashes ensure content integrity
 */

#ifndef BIFROST_BEP44_H
#define BIFROST_BEP44_H

#include <stdint.h>
#include <stddef.h>

/** @brief Maximum service advertisement size (FEAT-035) */
#define BEP44_REC_SVC_MAX 140

/**
 * @brief BEP-44 mutable record structure
 * 
 * Represents a signed record published by a bifrost node. Contains
 * reachability information, capabilities, and optional service advertisements.
 */
typedef struct {
    char          version[24];       /**< Record format version */
    uint32_t      ip;                /**< Reflexive endpoint IP (network byte order) */
    uint16_t      port;              /**< Reflexive endpoint port (network byte order) */
    unsigned char ula[16];           /**< ULA (IPv6 local address) */
    uint32_t      caps;              /**< Capability bitmask (idexch.h CAP_*) */
    unsigned char host_pubkey[32];   /**< SSH host Ed25519 key (optional) */
    unsigned char node_id[20];       /**< SHA256(account)[:20] for account-based lookup */
    uint32_t      route_ip;          /**< Route-hint IP (FEAT-031) */
    uint16_t      route_port;        /**< Route-hint port */
    uint8_t       nsvc;              /**< Number of advertised services */
    uint16_t      svc_len;           /**< Bytes used in svc[] */
    unsigned char svc[BEP44_REC_SVC_MAX]; /**< Service advertisements */
} bep44_record_t;

/**
 * @brief Compute DHT target from public key (no salt)
 * 
 * target = SHA1(pubkey)
 * 
 * @param pubkey Ed25519 public key (32 bytes)
 * @param target Output buffer (20 bytes)
 * 
 * @note Thread-safe
 * @note NULL-safe: Does nothing if pubkey or target is NULL
 */
void bep44_target(const unsigned char pubkey[32], unsigned char target[20]);

/**
 * @brief Compute DHT target from public key (mutable items)
 * 
 * target = SHA1("k" || pubkey)
 * 
 * @param pubkey Ed25519 public key (32 bytes)
 * @param target Output buffer (20 bytes)
 * 
 * @note Thread-safe
 * @note NULL-safe: Does nothing if pubkey or target is NULL
 */
void bep44_target_for_pubkey(unsigned char target[20], const unsigned char pubkey[32]);

/**
 * @brief Compute salted DHT target
 * 
 * target = SHA1(pubkey || salt)
 * 
 * Used for per-name signed DHT keys (spub/sget operations).
 * 
 * @param pubkey Ed25519 public key (32 bytes)
 * @param salt Salt value (max 64 bytes)
 * @param saltlen Length of salt
 * @param target Output buffer (20 bytes)
 * 
 * @note Thread-safe
 * @note NULL-safe: Does nothing if pubkey or target is NULL
 */
void bep44_target_salted(const unsigned char pubkey[32],
                         const unsigned char *salt, size_t saltlen,
                         unsigned char target[20]);

/**
 * @brief Compute DHT target for immutable value
 * 
 * target = SHA1(bencode(value))
 * 
 * Used for content-addressed immutable items.
 * 
 * @param v Value to hash
 * @param vlen Length of value
 * @param target Output buffer (20 bytes)
 * @return 0 on success, -1 if vlen > 1000 (BEP-44 limit)
 * 
 * @note Thread-safe
 * @note NULL-safe: Returns -1 if v or target is NULL
 * @note BEP-44: Rejects values > 1000 bytes (BUG-059)
 */
int bep44_immutable_target(const unsigned char *v, size_t vlen, unsigned char target[20]);

/**
 * @brief Build canonical buffer for signing (no salt)
 * 
 * Creates the canonical buffer: "3:seqi<seq>e1:v<vlen>:<value>"
 * 
 * @param seq Sequence number
 * @param value Value to sign
 * @param vlen Length of value
 * @param out Output buffer
 * @param outcap Capacity of output buffer
 * @return Buffer length, or -1 on overflow
 * 
 * @note Thread-safe
 * @note NULL-safe: Returns -1 if value or out is NULL
 */
int bep44_signbuf(uint32_t seq, const unsigned char *value, size_t vlen,
                  unsigned char *out, size_t outcap);

/**
 * @brief Build canonical buffer for signing (with salt)
 * 
 * Creates the canonical buffer: "4:salt<saltlen>:<salt>3:seqi<seq>e1:v<vlen>:<value>"
 * 
 * @param salt Salt value (NULL for no salt)
 * @param saltlen Length of salt
 * @param seq Sequence number
 * @param value Value to sign
 * @param vlen Length of value
 * @param out Output buffer
 * @param outcap Capacity of output buffer
 * @return Buffer length, or -1 on overflow
 * 
 * @note Thread-safe
 * @note NULL-safe: Returns -1 if value or out is NULL
 */
int bep44_signbuf_salted(const unsigned char *salt, size_t saltlen, uint32_t seq,
                         const unsigned char *value, size_t vlen,
                         unsigned char *out, size_t outcap);

/**
 * @brief Encode a BEP-44 record to wire format
 * 
 * @param r Record to encode
 * @param out Output buffer
 * @param outcap Capacity of output buffer
 * @return Bytes written, or -1 on error
 * 
 * @note Thread-safe
 * @note NULL-safe: Returns -1 if r or out is NULL
 * @note Wire format: ver_len(1) ver ip(4) port(2,BE) ula(16) caps(4,BE) host_pubkey(32) node_id(20)
 */
int bep44_record_encode(const bep44_record_t *r, unsigned char *out, size_t outcap);

/**
 * @brief Decode a BEP-44 record from wire format
 * 
 * @param in Input buffer (wire format)
 * @param len Length of input buffer
 * @param r Output record structure
 * @return 0 on success, -1 on error
 * 
 * @note Thread-safe
 * @note NULL-safe: Returns -1 if in or r is NULL
 * @note Backwards-compatible: node_id is optional (zeroed if missing)
 */
int bep44_record_decode(const unsigned char *in, size_t len, bep44_record_t *r);

/**
 * @brief Sign and encode a mutable item
 * 
 * Creates a BEP-44 mutable item: signs the value and produces wire format.
 * 
 * @param out Output buffer (wire format)
 * @param outcap Capacity of output buffer
 * @param pk Ed25519 public key (32 bytes)
 * @param value Value to sign
 * @param vlen Length of value
 * @param seq Sequence number
 * @param sk Ed25519 secret key (64 bytes)
 * @return Bytes written, or -1 on error
 * 
 * @note Thread-safe
 * @note NULL-safe: Returns -1 if out, pk, value, or sk is NULL
 * @note BEP-44: Max value length is 1000 bytes
 */
int bep44_encode(unsigned char *out, size_t outcap,
                 const unsigned char pk[32],
                 const unsigned char *value, size_t vlen,
                 uint32_t seq,
                 const unsigned char sk[64]);

/**
 * @brief Verify and decode a mutable item
 * 
 * Verifies the Ed25519 signature and extracts the value from wire format.
 * 
 * @param in Input buffer (wire format)
 * @param len Length of input buffer
 * @param pk Output: Ed25519 public key (32 bytes)
 * @param value Output: pointer to value (points into in)
 * @param vlen Output: length of value
 * @param seq Output: sequence number
 * @param sig Output: Ed25519 signature (64 bytes)
 * @return 0 on success, -1 on error (bad signature, invalid format)
 * 
 * @note Thread-safe
 * @note NULL-safe: Returns -1 if in, pk, value, vlen, or sig is NULL
 * @note Ownership: value points into in; do not free separately
 */
int bep44_decode(const unsigned char *in, size_t len,
                 unsigned char pk[32],
                 unsigned char **value, size_t *vlen,
                 uint32_t *seq,
                 unsigned char sig[64]);

#endif /* BIFROST_BEP44_H */