#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#define CRYPTO_PUBLICKEYBYTES 32
#define CRYPTO_SECRETKEYBYTES 64
#define CRYPTO_SIGNBYTES 64
#define NODE_ID_BYTES 32
#define ENCRYPTION_KEY_BYTES 32
#define ENCRYPTION_NONCE_BYTES 24
#define ENCRYPTION_OVERHEAD 16

typedef struct {
    unsigned char public_key[CRYPTO_PUBLICKEYBYTES];
    unsigned char secret_key[CRYPTO_SECRETKEYBYTES];
} keypair_t;

int crypto_init(void);
int crypto_keypair_new(keypair_t *kp);
int crypto_keypair_load(keypair_t *kp, const char *path);
int crypto_keypair_save(const keypair_t *kp, const char *path);

int bf_sign(unsigned char *sig, const unsigned char *msg, size_t msg_len, const unsigned char *secret_key);
int bf_verify(const unsigned char *sig, const unsigned char *msg, size_t msg_len, const unsigned char *public_key);

int bf_hash_name(const char *name, unsigned char *hash_out);

/* 1 if `name` is a fully-qualified host name (≥1 internal dot, non-empty labels, no
 * leading/trailing dot, no empty labels), else 0. Used to require an FQDN for VPN
 * pairing rather than guessing a domain (BUG-147). */
int bf_is_fqdn(const char *name);

/* Anonymous public-key encryption to an ed25519 identity (sealed box over the
 * X25519 form of the key). Used by `bifrost encrypt`/`decrypt`.
 * bf_seal: ed_pub is the recipient's 32-byte ed25519 public key; writes
 *   ptlen + BF_SEAL_OVERHEAD bytes to out. Returns 0 or -1.
 * bf_seal_open: ed_pub/ed_sk are the recipient's ed25519 keypair (sk is the
 *   64-byte libsodium secret); writes ctlen - BF_SEAL_OVERHEAD bytes. Returns 0
 *   on success, -1 on auth failure / bad key. */
#define BF_SEAL_OVERHEAD 48   /* crypto_box_SEALBYTES = 32 (eph pub) + 16 (MAC) */
int bf_seal(const unsigned char *ed_pub, const unsigned char *pt, size_t ptlen,
            unsigned char *out);
int bf_seal_open(const unsigned char *ed_pub, const unsigned char *ed_sk,
                 const unsigned char *ct, size_t ctlen, unsigned char *out);

uint32_t crypto_crc32c(const unsigned char *data, size_t len);

int crypto_xor_distance(const unsigned char *a, const unsigned char *b, unsigned char *result, size_t len);
int crypto_compare_distance(const unsigned char *a, const unsigned char *b);

int crypto_generate_node_id(unsigned char *node_id, uint32_t ip, uint8_t seed, unsigned char *pubkey);

#endif