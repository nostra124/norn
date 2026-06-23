/**
 * @file norn_suite_sodium.c
 * @brief Default libsodium cryptographic suite implementation.
 *
 * Implements the norn_crypto_suite_t interface using libsodium:
 * - Ed25519 for signatures
 * - X25519 for key exchange
 * - crypto_box (X25519-XSalsa20-Poly1305) for AEAD
 * - SHA-256 for hashing (BEP-44 mutable items)
 */

#include "norn_suite.h"
#include <sodium.h>
#include <string.h>

/* Sodium suite constants */
#define SODIUM_PUBKEY_LEN    32
#define SODIUM_SECRET_LEN    64
#define SODIUM_SIG_LEN       64
#define SODIUM_NODEID_LEN    32  /* SHA-256 output */
#define SODIUM_EPH_PUBKEY_LEN 32
#define SODIUM_EPH_SECRET_LEN 32
#define SODIUM_SHARED_LEN    32
#define SODIUM_AEAD_KEY_LEN  32
#define SODIUM_AEAD_NONCE_LEN 24
#define SODIUM_AEAD_OVERHEAD 16

static int sodium_sign(unsigned char *sig, const unsigned char *msg, size_t msg_len,
                       const unsigned char *secret_key) {
    if (!sig || !msg || !secret_key) return -1;
    return crypto_sign_detached(sig, NULL, msg, msg_len, secret_key);
}

static int sodium_verify(const unsigned char *sig, const unsigned char *msg, size_t msg_len,
                         const unsigned char *public_key) {
    if (!sig || !msg || !public_key) return -1;
    return crypto_sign_verify_detached(sig, msg, msg_len, public_key);
}

static int sodium_eph_keygen(unsigned char *eph_pubkey, unsigned char *eph_secret) {
    if (!eph_pubkey || !eph_secret) return -1;
    return crypto_box_keypair(eph_pubkey, eph_secret);
}

static int sodium_ecdh(unsigned char *shared, const unsigned char *my_eph_secret,
                       const unsigned char *peer_eph_pubkey) {
    if (!shared || !my_eph_secret || !peer_eph_pubkey) return -1;
    
    unsigned char client_k[crypto_box_BEFORENMBYTES];
    
    if (crypto_box_beforenm(client_k, peer_eph_pubkey, my_eph_secret) != 0) {
        return -1;
    }
    
    /* Use first 32 bytes of shared secret */
    memset(shared, 0, SODIUM_SHARED_LEN);
    memcpy(shared, client_k, crypto_box_BEFORENMBYTES < SODIUM_SHARED_LEN ? 
           crypto_box_BEFORENMBYTES : SODIUM_SHARED_LEN);
    
    return 0;
}

static int sodium_aead_seal(unsigned char *out, size_t *out_len, const unsigned char *pt,
                            size_t pt_len, const unsigned char *key, const unsigned char *nonce) {
    if (!out || !pt || !key || !nonce) return -1;
    
    int ret = crypto_secretbox_easy(out, pt, pt_len, nonce, key);
    if (ret != 0) return -1;
    
    if (out_len) *out_len = pt_len + crypto_secretbox_MACBYTES;
    return 0;
}

static int sodium_aead_open(unsigned char *out, size_t *out_len, const unsigned char *ct,
                             size_t ct_len, const unsigned char *key, const unsigned char *nonce) {
    if (!out || !ct || !key || !nonce) return -1;
    if (ct_len < crypto_secretbox_MACBYTES) return -1;
    
    int ret = crypto_secretbox_open_easy(out, ct, ct_len, nonce, key);
    if (ret != 0) return -1;
    
    if (out_len) *out_len = ct_len - crypto_secretbox_MACBYTES;
    return 0;
}

static int sodium_nodeid_from_pubkey(unsigned char *node_id, const unsigned char *public_key) {
    if (!node_id || !public_key) return -1;
    
    /* Use SHA-256 of public key for node ID */
    crypto_hash_sha256(node_id, public_key, SODIUM_PUBKEY_LEN);
    return 0;
}

static int sodium_hash(unsigned char *out, const unsigned char *data, size_t data_len) {
    if (!out || !data) return -1;
    crypto_hash_sha256(out, data, data_len);
    return 0;
}

static const norn_crypto_suite_t sodium_suite = {
    .name = "sodium",
    .pubkey_len = SODIUM_PUBKEY_LEN,
    .secret_len = SODIUM_SECRET_LEN,
    .sig_len = SODIUM_SIG_LEN,
    .nodeid_len = SODIUM_NODEID_LEN,
    .eph_pubkey_len = SODIUM_EPH_PUBKEY_LEN,
    .eph_secret_len = SODIUM_EPH_SECRET_LEN,
    .shared_len = SODIUM_SHARED_LEN,
    .aead_key_len = SODIUM_AEAD_KEY_LEN,
    .aead_nonce_len = SODIUM_AEAD_NONCE_LEN,
    .aead_overhead = SODIUM_AEAD_OVERHEAD,
    
    .sign = sodium_sign,
    .verify = sodium_verify,
    .eph_keygen = sodium_eph_keygen,
    .ecdh = sodium_ecdh,
    .aead_seal = sodium_aead_seal,
    .aead_open = sodium_aead_open,
    .nodeid_from_pubkey = sodium_nodeid_from_pubkey,
    .hash = sodium_hash,
};

const norn_crypto_suite_t *norn_suite_sodium(void) {
    return &sodium_suite;
}