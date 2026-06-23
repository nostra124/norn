/**
 * @file norn_suite.h
 * @brief Pluggable cryptography suite interface.
 *
 * norn supports multiple crypto backends through a suite vtable.
 * The default suite uses libsodium (Ed25519/X25519/NaCl), but applications
 * can install custom suites (e.g., secp256k1/ChaCha20-Poly1305 for Nostr).
 *
 * @section usage Basic Usage
 *
 * Default sodium suite (no configuration needed):
 * @code
 * norn_client_t *client = norn_new(pubkey, seckey, NULL);
 * // Uses norn_suite_sodium() by default
 * @endcode
 *
 * Custom suite:
 * @code
 * norn_config_t cfg = {0};
 * cfg.suite = my_custom_suite();  // Application-defined
 * norn_client_t *client = norn_new(pubkey, seckey, &cfg);
 * @endcode
 *
 * @section implementing Implementing a Custom Suite
 *
 * A suite must implement all function pointers in norn_crypto_suite_t.
 * The suite is installed at client creation and used for:
 * - Identity signing/verification
 * - Ephemeral key generation for session handshake
 * - ECDH shared secret derivation
 * - AEAD seal/open for encrypted channels
 * - Node ID derivation from public key
 *
 * @section thread_safety Thread Safety
 * Suite functions may be called concurrently. Implementations must be thread-safe.
 */

#ifndef NORN_SUITE_H
#define NORN_SUITE_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Cryptographic suite vtable.
 *
 * Defines all cryptographic operations used by norn. Applications can
 * implement custom suites to use different primitives (Ed25519, secp256k1, etc.).
 *
 * All functions return 0 on success, -1 on error unless otherwise noted.
 */
typedef struct norn_crypto_suite norn_crypto_suite_t;

struct norn_crypto_suite {
    /* ========================================================================
     * Suite metadata
     * ======================================================================== */
    
    /** @brief Human-readable suite name (e.g., "sodium", "secp256k1") */
    const char *name;
    
    /** @brief Public key size in bytes (e.g., 32 for Ed25519, 33 for secp256k1 compressed) */
    size_t pubkey_len;
    
    /** @brief Secret key size in bytes (e.g., 64 for Ed25519, 32 for secp256k1) */
    size_t secret_len;
    
    /** @brief Signature size in bytes (e.g., 64 for Ed25519, 64-72 for ECDSA) */
    size_t sig_len;
    
    /** @brief Node ID size in bytes (e.g., 20 for SHA-1, 32 for SHA-256) */
    size_t nodeid_len;
    
    /** @brief Ephemeral public key size in bytes */
    size_t eph_pubkey_len;
    
    /** @brief Ephemeral secret key size in bytes */
    size_t eph_secret_len;
    
    /** @brief Shared secret size in bytes (ECDH output) */
    size_t shared_len;
    
    /** @brief AEAD key size in bytes */
    size_t aead_key_len;
    
    /** @brief AEAD nonce size in bytes */
    size_t aead_nonce_len;
    
    /** @brief AEAD overhead (MAC + auth tag) in bytes */
    size_t aead_overhead;
    
    /* ========================================================================
     * Identity operations
     * ======================================================================== */
    
    /**
     * @brief Sign a message.
     *
     * @param sig Output buffer for signature (sig_len bytes)
     * @param msg Message to sign
     * @param msg_len Message length
     * @param secret_key Secret key (secret_len bytes)
     * @return 0 on success, -1 on error
     */
    int (*sign)(unsigned char *sig, const unsigned char *msg, size_t msg_len,
                const unsigned char *secret_key);
    
    /**
     * @brief Verify a signature.
     *
     * @param sig Signature to verify (sig_len bytes)
     * @param msg Message that was signed
     * @param msg_len Message length
     * @param public_key Public key (pubkey_len bytes)
     * @return 0 on valid signature, -1 on invalid or error
     */
    int (*verify)(const unsigned char *sig, const unsigned char *msg, size_t msg_len,
                  const unsigned char *public_key);
    
    /* ========================================================================
     * Ephemeral session crypto
     * ======================================================================== */
    
    /**
     * @brief Generate an ephemeral keypair for session handshake.
     *
     * @param eph_pubkey Output buffer for ephemeral public key (eph_pubkey_len bytes)
     * @param eph_secret Output buffer for ephemeral secret key (eph_secret_len bytes)
     * @return 0 on success, -1 on error
     */
    int (*eph_keygen)(unsigned char *eph_pubkey, unsigned char *eph_secret);
    
    /**
     * @brief Derive a shared secret via ECDH.
     *
     * @param shared Output buffer for shared secret (shared_len bytes)
     * @param my_eph_secret Our ephemeral secret key (eph_secret_len bytes)
     * @param peer_eph_pubkey Peer's ephemeral public key (eph_pubkey_len bytes)
     * @return 0 on success, -1 on error
     */
    int (*ecdh)(unsigned char *shared, const unsigned char *my_eph_secret,
                const unsigned char *peer_eph_pubkey);
    
    /**
     * @brief AEAD encrypt (seal).
     *
     * @param out Output buffer for ciphertext (msg_len + aead_overhead bytes)
     * @param out_len Output length, or NULL
     * @param pt Plaintext to encrypt
     * @param pt_len Plaintext length
     * @param key AEAD key (aead_key_len bytes)
     * @param nonce AEAD nonce (aead_nonce_len bytes)
     * @return 0 on success, -1 on error
     */
    int (*aead_seal)(unsigned char *out, size_t *out_len, const unsigned char *pt,
                     size_t pt_len, const unsigned char *key, const unsigned char *nonce);
    
    /**
     * @brief AEAD decrypt (open).
     *
     * @param out Output buffer for plaintext (ct_len - aead_overhead bytes max)
     * @param out_len Output length, or NULL
     * @param ct Ciphertext to decrypt
     * @param ct_len Ciphertext length
     * @param key AEAD key (aead_key_len bytes)
     * @param nonce AEAD nonce (aead_nonce_len bytes)
     * @return 0 on success, -1 on authentication failure
     */
    int (*aead_open)(unsigned char *out, size_t *out_len, const unsigned char *ct,
                     size_t ct_len, const unsigned char *key, const unsigned char *nonce);
    
    /* ========================================================================
     * DHT addressing
     * ======================================================================== */
    
    /**
     * @brief Derive a DHT node ID from a public key.
     *
     * Different suites may use different derivation methods:
     * - Sodium: SHA-1 or SHA-256 of public key
     * - secp256k1: Hash of compressed public key
     *
     * @param node_id Output buffer for node ID (nodeid_len bytes)
     * @param public_key Public key (pubkey_len bytes)
     * @return 0 on success, -1 on error
     */
    int (*nodeid_from_pubkey)(unsigned char *node_id, const unsigned char *public_key);
    
    /**
     * @brief Hash arbitrary data (for BEP-44 mutable items).
     *
     * @param out Output buffer (nodeid_len bytes)
     * @param data Data to hash
     * @param data_len Data length
     * @return 0 on success, -1 on error
     */
    int (*hash)(unsigned char *out, const unsigned char *data, size_t data_len);
};

/* ============================================================================
 * Built-in suites
 * ============================================================================ */

/**
 * @brief Get the default libsodium suite.
 *
 * Uses Ed25519 for signatures, X25519 for key exchange, and
 * crypto_box (X25519-XSalsa20-Poly1305) for AEAD.
 *
 * @return Pointer to the sodium suite (never NULL)
 */
const norn_crypto_suite_t *norn_suite_sodium(void);

#endif /* NORN_SUITE_H */