#ifndef NORN_H
#define NORN_H

/* norn — Mainline DHT client library (named after the Norns of Nordic mythology).
 * Provides reusable DHT functionality for peer discovery and bootstrap.
 * In-memory only: configuration and data are fed by the application. */

#include <stdint.h>
#include <stddef.h>

#define NORN_ID_BYTES      20   /* DHT node ID (SHA-1) */
#define NORN_PUBKEY_BYTES  32   /* ed25519 public key */
#define NORN_SECRETKEY_BYTES 64  /* ed25519 secret key */

/* Opaque handles */
typedef struct norn_client norn_client_t;
typedef struct norn_record norn_record_t;

/* Configuration (passed to norn_new) */
typedef struct {
    const char *version;                  /* Application version string */
    int read_only;                         /* BEP-43: read-only mode */
    int private_mode;                      /* Bootstrap only to boot_* peers */
    
    /* Bootstrap peers (may be NULL for default mainline) */
    const uint32_t *boot_ips;
    const uint16_t *boot_ports;
    int boot_count;
    
    /* Logging callback (may be NULL) */
    void (*log_func)(const char *fmt, ...);
} norn_config_t;

/* Record (BEP-44 mutable signed) */
typedef struct {
    unsigned char key[NORN_ID_BYTES];      /* target = sha1("k" || pubkey) */
    unsigned char pubkey[NORN_PUBKEY_BYTES];
    unsigned char value[1024];              /* BEP-44 max value size */
    size_t value_len;
    uint32_t seq;
    unsigned char sig[64];
    int have_sig;                          /* 1 if sig is valid */
} norn_mutable_t;

/* Callback for async get operations */
typedef void (*norn_get_callback_t)(void *user_data,
                                    const unsigned char *value, size_t value_len);

/* Callback for peer discovery */
typedef void (*norn_peer_callback_t)(void *user_data,
                                     const unsigned char *pubkey, uint32_t ip, uint16_t port);

/* === Client lifecycle === */

/* Create a new DHT client. Returns NULL on failure. */
norn_client_t *norn_new(const unsigned char *self_pub,
                        const unsigned char *self_sec,
                        const norn_config_t *cfg);

/* Destroy a DHT client. NULL-safe. */
void norn_free(norn_client_t *client);

/* Get the client's DHT node ID (20 bytes). */
int norn_get_id(const norn_client_t *client, unsigned char out[NORN_ID_BYTES]);

/* === DHT operations === */

/* Bootstrap to the DHT (find nodes). Returns 0 on success, -1 on error. */
int norn_bootstrap(norn_client_t *client);

/* Put a mutable signed record (BEP-44). Returns 0 on success, -1 on error.
 * The value is signed with the keypair and stored at key = sha1("k" || pubkey). */
int norn_put_mutable(norn_client_t *client,
                     const unsigned char *pubkey, const unsigned char *secret,
                     const unsigned char *value, size_t value_len,
                     uint32_t seq);

/* Get a mutable signed record. Async; callback is invoked when found. */
int norn_get_mutable(norn_client_t *client,
                     const unsigned char *pubkey,
                     norn_get_callback_t callback, void *user_data);

/* Put an immutable value. Returns 0 on success, -1 on error.
 * Key is sha1(value). */
int norn_put_immutable(norn_client_t *client,
                       const unsigned char *value, size_t value_len);

/* Get an immutable value by key (sha1). Async. */
int norn_get_immutable(norn_client_t *client,
                       const unsigned char *key,
                       norn_get_callback_t callback, void *user_data);

/* Announce that we are a peer for the given info_hash (DHT server mode). */
int norn_announce(norn_client_t *client,
                  const unsigned char *info_hash);

/* Discover peers for the given info_hash. Async; callback for each peer found. */
int norn_discover(norn_client_t *client,
                  const unsigned char *info_hash,
                  norn_peer_callback_t callback, void *user_data);

/* === Event loop integration === */

/* Process pending DHT transactions. Call regularly (e.g., every 100ms).
 * Returns the number of transactions processed. */
int norn_tick(norn_client_t *client);

/* Get the socket FD for select()/poll() integration. */
int norn_get_fd(const norn_client_t *client);

/* === Record codec === */

/* Encode a mutable record for storage/transmission. Returns bytes written, -1 on error. */
int norn_encode_mutable(const norn_mutable_t *rec,
                        unsigned char *out, size_t outcap);

/* Decode a mutable record. Returns 0 on success, -1 on error. */
int norn_decode_mutable(const unsigned char *buf, size_t len,
                        norn_mutable_t *rec);

#endif /* NORN_H */