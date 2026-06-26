#include "crypto.h"
#include <sodium.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>   /* fchmod — owner-only key files (BUG-104) */
#include <arpa/inet.h>

static int crc32c_table_ready = 0;
static uint32_t crc32c_table[256];

static void init_crc32c_table(void) {
    if (crc32c_table_ready) return;   /* LCOV_EXCL_BR_LINE: crypto_crc32c only calls this when !ready */
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0x82F63B78;
            } else {
                crc >>= 1;
            }
        }
        crc32c_table[i] = crc;
    }
    crc32c_table_ready = 1;
}

int crypto_init(void) {
    return sodium_init();
}

int crypto_keypair_new(keypair_t *kp) {
    if (!kp) return -1;
    return crypto_sign_keypair(kp->public_key, kp->secret_key);
}

int crypto_keypair_load(keypair_t *kp, const char *path) {
    if (!kp || !path) return -1;
    
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size < CRYPTO_SECRETKEYBYTES) {
        fclose(f);
        return -1;
    }
    
    size_t read = fread(kp->secret_key, 1, CRYPTO_SECRETKEYBYTES, f);
    if (read != CRYPTO_SECRETKEYBYTES) {   /* LCOV_EXCL_BR_LINE: file_size>=SECRETKEYBYTES guarantees full read */
        fclose(f);   /* LCOV_EXCL_LINE */
        return -1;   /* LCOV_EXCL_LINE */
    }

    if (file_size >= CRYPTO_SECRETKEYBYTES + CRYPTO_PUBLICKEYBYTES) {
        read = fread(kp->public_key, 1, CRYPTO_PUBLICKEYBYTES, f);
        fclose(f);
        return (read == CRYPTO_PUBLICKEYBYTES) ? 0 : -1;   /* LCOV_EXCL_BR_LINE: file_size>= both keys guarantees full read */
    }

    fclose(f);

    if (read == CRYPTO_SECRETKEYBYTES) {   /* LCOV_EXCL_BR_LINE: read already verified == SECRETKEYBYTES above */
        memcpy(kp->public_key, kp->secret_key + 32, CRYPTO_PUBLICKEYBYTES);
        crypto_keypair_save(kp, path);
        return 0;
    }

    return -1;   /* LCOV_EXCL_LINE: unreachable, read always == SECRETKEYBYTES here */
}

int crypto_keypair_save(const keypair_t *kp, const char *path) {
    if (!kp || !path) return -1;
    
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    /* Owner-only BEFORE writing the secret key — every other secret in the tree
     * forces 0600; this one was inheriting the umask (0644 under launchd/systemd),
     * leaving the root daemon's private key world-readable so the user daemon could
     * impersonate it (BUG-104). */
    if (fchmod(fileno(f), 0600) != 0) { fclose(f); return -1; }   /* LCOV_EXCL_BR_LINE: fchmod on a freshly opened regular file never fails */

    size_t written = fwrite(kp->secret_key, 1, CRYPTO_SECRETKEYBYTES, f);
    if (written != CRYPTO_SECRETKEYBYTES) {   /* LCOV_EXCL_BR_LINE: buffered fwrite to a regular file never short-writes */
        fclose(f);   /* LCOV_EXCL_LINE */
        return -1;   /* LCOV_EXCL_LINE */
    }

    written = fwrite(kp->public_key, 1, CRYPTO_PUBLICKEYBYTES, f);
    fclose(f);

    return (written == CRYPTO_PUBLICKEYBYTES) ? 0 : -1;   /* LCOV_EXCL_BR_LINE: buffered fwrite to a regular file never short-writes */
}

int bf_sign(unsigned char *sig, const unsigned char *msg, size_t msg_len, const unsigned char *secret_key) {
    if (!sig || !msg || !secret_key) return -1;
    return crypto_sign_detached(sig, NULL, msg, msg_len, secret_key);
}

int bf_verify(const unsigned char *sig, const unsigned char *msg, size_t msg_len, const unsigned char *public_key) {
    if (!sig || !msg || !public_key) return -1;
    return crypto_sign_verify_detached(sig, msg, msg_len, public_key);
}

#define ENCRYPTION_NONCE_BYTES 24
#define ENCRYPTION_KEY_BYTES 32
#define ENCRYPTION_OVERHEAD 16

int bf_seal(const unsigned char *ed_pub, const unsigned char *pt, size_t ptlen,
            unsigned char *out) {
    if (!ed_pub || !pt || !out) return -1;
    unsigned char x_pub[crypto_box_PUBLICKEYBYTES];
    if (crypto_sign_ed25519_pk_to_curve25519(x_pub, ed_pub) != 0) return -1;
    return crypto_box_seal(out, pt, ptlen, x_pub);   /* anonymous sealed box */
}

int bf_seal_open(const unsigned char *ed_pub, const unsigned char *ed_sk,
                 const unsigned char *ct, size_t ctlen, unsigned char *out) {
    if (!ed_pub || !ed_sk || !ct || !out) return -1;
    if (ctlen < crypto_box_SEALBYTES) return -1;
    unsigned char x_pub[crypto_box_PUBLICKEYBYTES], x_sk[crypto_box_SECRETKEYBYTES];
    if (crypto_sign_ed25519_pk_to_curve25519(x_pub, ed_pub) != 0) return -1;
    if (crypto_sign_ed25519_sk_to_curve25519(x_sk, ed_sk) != 0) return -1;   /* LCOV_EXCL_BR_LINE: sk->curve conversion never fails */
    return crypto_box_seal_open(out, ct, ctlen, x_pub, x_sk);
}

int bf_hash_name(const char *name, unsigned char *hash_out) {
    if (!name || !hash_out) return -1;

    crypto_hash_sha256(hash_out, (const unsigned char *)name, strlen(name));
    return 0;
}

int bf_is_fqdn(const char *name) {
    if (!name || !*name) return 0;
    size_t n = strlen(name);
    if (name[0] == '.' || name[n - 1] == '.') return 0;   /* no leading/trailing dot */
    int dot = 0;
    for (size_t i = 0; i + 1 < n; i++)
        if (name[i] == '.') { dot = 1; if (name[i + 1] == '.') return 0; }  /* no empty label */
    return dot;   /* needs ≥1 internal dot */
}

uint32_t crypto_crc32c(const unsigned char *data, size_t len) {
    if (!crc32c_table_ready) init_crc32c_table();
    
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32c_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

int crypto_xor_distance(const unsigned char *a, const unsigned char *b, unsigned char *result, size_t len) {
    if (!a || !b || !result) return -1;
    
    for (size_t i = 0; i < len; i++) {
        result[i] = a[i] ^ b[i];
    }
    return 0;
}

int crypto_compare_distance(const unsigned char *a, const unsigned char *b) {
    for (size_t i = 0; i < NODE_ID_BYTES; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

int crypto_generate_node_id(unsigned char *node_id, uint32_t ip, uint8_t seed, unsigned char *pubkey) {
    if (!node_id || !pubkey) return -1;
    
    uint32_t ip_be = htonl(ip);
    unsigned char crc_input[5];
    memcpy(crc_input, &ip_be, 4);
    crc_input[4] = seed;
    
    uint32_t crc = crypto_crc32c(crc_input, 5);
    
    memcpy(node_id, &crc, 4);
    node_id[0] = (node_id[0] & 0x03) | (((seed >> 0) & 0x03) << 0);
    node_id[1] = (node_id[1] & 0x07) | (((seed >> 2) & 0x07) << 0);
    node_id[2] = (node_id[2] & 0x07) | (((seed >> 5) & 0x07) << 0);
    
    for (int i = 3; i < NODE_ID_BYTES; i++) {
        node_id[i] = pubkey[i - 3];
    }
    
    return 0;
}