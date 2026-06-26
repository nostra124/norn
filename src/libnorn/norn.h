/**
 * @file norn.h
 * @brief Mainline DHT client library for P2P peer discovery and bootstrap.
 * 
 * Named after the Norns of Nordic mythology who control destiny, this library
 * provides reusable DHT functionality for peer discovery and bootstrap.
 * 
 * @par Key Features
 * - BEP-5 DHT protocol implementation
 * - BEP-44 mutable and immutable items
 * - BEP-43 read-only mode
 * - Event-loop compatible (non-blocking)
 * - In-memory only (no config files)
 * 
 * @par Basic Usage
 * @code
 * #include "norn.h"
 * 
 * int main(void) {
 *     unsigned char pubkey[32], secret[64];
 *     crypto_keypair_new(&(keypair_t){.public_key = pubkey, .secret_key = secret});
 *     
 *     norn_config_t cfg = {.version = "1.0"};
 *     norn_client_t *client = norn_new(pubkey, secret, &cfg);
 *     if (!client) return 1;
 *     
 *     norn_bootstrap(client);
 *     
 *     while (1) {
 *         norn_tick(client);
 *         usleep(100000);
 *     }
 *     
 *     norn_free(client);
 *     return 0;
 * }
 * @endcode
 * 
 * @par Thread Safety
 * All functions are single-threaded. Caller must synchronize if using from
 * multiple threads.
 * 
 * @par Memory Management
 * - Client allocates norn_client_t, all other memory managed internally
 * - All buffers are caller-owned unless explicitly documented
 * - No heap allocations in hot paths (uses arena/pool allocators)
 * 
 * @par Error Handling
 * - int functions: return 0 on success, -1 on error
 * - pointer functions: return valid pointer or NULL on error
 * - All functions are NULL-safe (handle NULL inputs gracefully)
 */

#ifndef NORN_H
#define NORN_H

#include <stdint.h>
#include <stddef.h>

/** @brief DHT node ID size (SHA-1 hash, 20 bytes) */
#define NORN_ID_BYTES      20

/** @brief Ed25519 public key size (32 bytes) */
#define NORN_PUBKEY_BYTES  32

/** @brief Ed25519 secret key size (64 bytes) */
#define NORN_SECRETKEY_BYTES 64

/* Opaque handles */
typedef struct norn_client norn_client_t;
typedef struct norn_record norn_record_t;

/**
 * @brief Client configuration
 * 
 * Passed to norn_new() to configure the DHT client. All pointers are borrowed
 * (not copied) and must remain valid for the lifetime of the client.
 */
typedef struct {
    const char *version;                  /**< Application version string (e.g., "bifrost/1.0") */
    int read_only;                        /**< BEP-43: read-only mode (don't respond to queries) */
    int private_mode;                      /**< Bootstrap only to boot_* peers (no public DHT) */
    
    const uint32_t *boot_ips;              /**< Bootstrap peer IPs (network byte order), NULL for default */
    const uint16_t *boot_ports;            /**< Bootstrap peer ports (network byte order), NULL for default */
    int boot_count;                        /**< Number of bootstrap peers */
    
    void (*log_func)(const char *fmt, ...); /**< Logging callback, NULL for stderr */
} norn_config_t;

/**
 * @brief Mutable signed record (BEP-44)
 * 
 * Represents a mutable DHT item signed by an Ed25519 keypair.
 * The target (DHT key) is SHA1("k" || pubkey).
 */
typedef struct {
    unsigned char key[NORN_ID_BYTES];      /**< Target (DHT key) = SHA1("k" || pubkey) */
    unsigned char pubkey[NORN_PUBKEY_BYTES]; /**< Ed25519 public key */
    unsigned char value[1024];              /**< BEP-44 max value size (1000 bytes) */
    size_t value_len;                       /**< Actual value length */
    uint32_t seq;                            /**< Monotonically increasing sequence number */
    unsigned char sig[64];                   /**< Ed25519 signature over canonical buffer */
    int have_sig;                            /**< 1 if sig is valid, 0 if not verified */
} norn_mutable_t;

/**
 * @brief Callback for async get operations
 * 
 * Invoked when a mutable or immutable value is retrieved from the DHT.
 * May be called multiple times for get_mutable if multiple values exist.
 * 
 * @param user_data User-provided pointer from norn_get_mutable/norn_get_immutable
 * @param value Retrieved value (caller-owned, valid only during callback)
 * @param value_len Length of value in bytes
 */
typedef void (*norn_get_callback_t)(void *user_data,
                                    const unsigned char *value, size_t value_len);

/**
 * @brief Callback for peer discovery
 * 
 * Invoked for each peer discovered for an info_hash. May be called multiple
 * times as peers are discovered.
 * 
 * @param user_data User-provided pointer from norn_discover
 * @param pubkey Peer's Ed25519 public key (32 bytes, caller-owned)
 * @param ip Peer's IP address (network byte order)
 * @param port Peer's port (network byte order)
 */
typedef void (*norn_peer_callback_t)(void *user_data,
                                     const unsigned char *pubkey, uint32_t ip, uint16_t port);

/* === Client lifecycle === */

/**
 * @brief Create a new DHT client
 * 
 * Initializes a DHT client with the given keypair and configuration.
 * The client starts in an un-bootstrapped state; call norn_bootstrap()
 * to join the DHT network.
 * 
 * @param self_pub Ed25519 public key (32 bytes, caller-owned)
 * @param self_sec Ed25519 secret key (64 bytes, caller-owned)
 * @param cfg Configuration (may be NULL for defaults)
 * @return New client handle, or NULL on error (invalid params, allocation failure)
 * 
 * @note Thread Safety: Not thread-safe. Create one client per thread or synchronize.
 * @note Ownership: Caller owns the returned handle; free with norn_free()
 * @note NULL-safe: Returns NULL if self_pub or self_sec is NULL
 * 
 * @code
 * keypair_t kp;
 * crypto_keypair_new(&kp);
 * 
 * norn_config_t cfg = {.version = "myapp/1.0"};
 * norn_client_t *client = norn_new(kp.public_key, kp.secret_key, &cfg);
 * if (!client) {
 *     fprintf(stderr, "Failed to create client\n");
 *     return 1;
 * }
 * @endcode
 */
norn_client_t *norn_new(const unsigned char *self_pub,
                        const unsigned char *self_sec,
                        const norn_config_t *cfg);

/**
 * @brief Destroy a DHT client
 * 
 * Releases all resources associated with the client. The client must not
 * be used after this call.
 * 
 * @param client Client handle (may be NULL)
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Does nothing if client is NULL
 */
void norn_free(norn_client_t *client);

/**
 * @brief Get the client's DHT node ID
 * 
 * Returns the SHA-256 hash of the public key, truncated to 20 bytes.
 * This is the client's identifier in the DHT network.
 * 
 * @param client Client handle
 * @param out Buffer for node ID (20 bytes)
 * @return 0 on success, -1 on error (NULL params)
 * 
 * @note Thread Safety: Thread-safe for reading
 * @note NULL-safe: Returns -1 if client or out is NULL
 * 
 * @code
 * unsigned char id[20];
 * if (norn_get_id(client, id) == 0) {
 *     printf("Node ID: ");
 *     for (int i = 0; i < 20; i++) printf("%02x", id[i]);
 *     printf("\n");
 * }
 * @endcode
 */
int norn_get_id(const norn_client_t *client, unsigned char out[NORN_ID_BYTES]);

/* === DHT operations === */

/**
 * @brief Bootstrap to the DHT network
 * 
 * Sends find_node queries to bootstrap peers to join the DHT network.
 * The client will discover more nodes through the bootstrap process.
 * 
 * @param client Client handle
 * @return 0 on success (queries sent), -1 on error
 * 
 * @note Async: Returns immediately; bootstrap happens over time
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns -1 if client is NULL
 * @note Idempotent: Safe to call multiple times
 * 
 * @code
 * if (norn_bootstrap(client) != 0) {
 *     fprintf(stderr, "Bootstrap failed\n");
 *     return 1;
 * }
 * 
 * // Wait for bootstrap to complete
 * while (!bootstrap_complete) {
 *     norn_tick(client);
 *     usleep(100000);
 * }
 * @endcode
 */
int norn_bootstrap(norn_client_t *client);

/**
 * @brief Store a mutable signed record in the DHT (BEP-44)
 * 
 * Publishes a mutable item to the DHT. The item is signed with the provided
 * keypair and stored at target = SHA1("k" || pubkey).
 * 
 * @param client Client handle
 * @param pubkey Ed25519 public key (32 bytes)
 * @param secret Ed25519 secret key (64 bytes)
 * @param value Value to store (max 1000 bytes per BEP-44)
 * @param value_len Length of value in bytes
 * @param seq Sequence number (must be monotonically increasing for same pubkey)
 * @return 0 on success, -1 on error
 * 
 * @note Async: Returns immediately; storage happens over time
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns -1 if client, pubkey, or secret is NULL
 * @note Ownership: Value is copied; caller retains ownership
 * @note BEP-44: Implements mutable items with sequence numbers
 * 
 * @code
 * keypair_t kp;
 * crypto_keypair_new(&kp);
 * 
 * const char *value = "Hello, DHT!";
 * uint32_t seq = 1;
 * 
 * if (norn_put_mutable(client, kp.public_key, kp.secret_key,
 *                      (const unsigned char *)value, strlen(value), seq) != 0) {
 *     fprintf(stderr, "Put failed\n");
 * }
 * @endcode
 */
int norn_put_mutable(norn_client_t *client,
                     const unsigned char *pubkey, const unsigned char *secret,
                     const unsigned char *value, size_t value_len,
                     uint32_t seq);

/**
 * @brief Retrieve a mutable signed record from the DHT (BEP-44)
 * 
 * Asynchronously retrieves a mutable item from the DHT. The callback is invoked
 * when the item is found, possibly multiple times if multiple values exist.
 * 
 * @param client Client handle
 * @param pubkey Ed25519 public key (32 bytes) - identifies the record
 * @param callback Function to call when value is found
 * @param user_data User data passed to callback
 * @return 0 on success (query sent), -1 on error
 * 
 * @note Async: Returns immediately; callback invoked later
 * @note Thread Safety: Not thread-safe (callback runs in norn_tick thread)
 * @note NULL-safe: Returns -1 if client, pubkey, or callback is NULL
 * @note Callback: May be called multiple times
 * @note Ownership: value is valid only during callback; copy if needed
 * 
 * @code
 * void on_value(void *user_data, const unsigned char *value, size_t value_len) {
 *     printf("Got value: %.*s\n", (int)value_len, value);
 * }
 * 
 * unsigned char pubkey[32];
 * // ... fill pubkey ...
 * 
 * if (norn_get_mutable(client, pubkey, on_value, NULL) != 0) {
 *     fprintf(stderr, "Get failed\n");
 * }
 * 
 * // Process callbacks
 * while (1) {
 *     norn_tick(client);
 *     usleep(100000);
 * }
 * @endcode
 */
int norn_get_mutable(norn_client_t *client,
                     const unsigned char *pubkey,
                     norn_get_callback_t callback, void *user_data);

/**
 * @brief Store an immutable value in the DHT (BEP-44)
 * 
 * Publishes an immutable item to the DHT. The key is SHA1(bencode(value)),
 * making it content-addressed and self-verifying (no signature needed).
 * 
 * @param client Client handle
 * @param value Value to store (max 1000 bytes per BEP-44)
 * @param value_len Length of value in bytes
 * @return 0 on success (query sent), -1 on error
 * 
 * @note Async: Returns immediately; storage happens over time
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns -1 if client or value is NULL
 * @note Ownership: Value is copied; caller retains ownership
 * @note BEP-44: Implements immutable items (content-addressed)
 * 
 * @code
 * const char *value = "Hello, DHT!";
 * if (norn_put_immutable(client, (const unsigned char *)value, strlen(value)) != 0) {
 *     fprintf(stderr, "Put immutable failed\n");
 * }
 * @endcode
 */
int norn_put_immutable(norn_client_t *client,
                       const unsigned char *value, size_t value_len);

/**
 * @brief Retrieve an immutable value from the DHT (BEP-44)
 * 
 * Asynchronously retrieves an immutable item from the DHT by its key (SHA1 hash).
 * 
 * @param client Client handle
 * @param key SHA1 hash of the value (20 bytes)
 * @param callback Function to call when value is found
 * @param user_data User data passed to callback
 * @return 0 on success (query sent), -1 on error
 * 
 * @note Async: Returns immediately; callback invoked later
 * @note Thread Safety: Not thread-safe (callback runs in norn_tick thread)
 * @note NULL-safe: Returns -1 if client, key, or callback is NULL
 * 
 * @code
 * void on_value(void *user_data, const unsigned char *value, size_t value_len) {
 *     printf("Got immutable: %.*s\n", (int)value_len, value);
 * }
 * 
 * unsigned char key[20];
 * // ... compute SHA1 of value ...
 * 
 * if (norn_get_immutable(client, key, on_value, NULL) != 0) {
 *     fprintf(stderr, "Get immutable failed\n");
 * }
 * @endcode
 */
int norn_get_immutable(norn_client_t *client,
                       const unsigned char *key,
                       norn_get_callback_t callback, void *user_data);

/**
 * @brief Announce peer for an info_hash (DHT server mode)
 * 
 * Announces that this client is a peer for the given info_hash. Other DHT
 * clients can discover this peer via norn_discover().
 * 
 * @param client Client handle
 * @param info_hash Info hash to announce (20 bytes)
 * @return 0 on success (announcement sent), -1 on error
 * 
 * @note Async: Returns immediately; announcement happens over time
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns -1 if client or info_hash is NULL
 * @note BEP-5: Implements DHT announce_peer
 */
int norn_announce(norn_client_t *client,
                  const unsigned char *info_hash);

/**
 * @brief Discover peers for an info_hash
 * 
 * Asynchronously discovers peers that have announced for the given info_hash.
 * The callback is invoked for each peer discovered.
 * 
 * @param client Client handle
 * @param info_hash Info hash to discover (20 bytes)
 * @param callback Function to call for each peer found
 * @param user_data User data passed to callback
 * @return 0 on success (query sent), -1 on error
 * 
 * @note Async: Returns immediately; callback invoked for each peer
 * @note Thread Safety: Not thread-safe (callback runs in norn_tick thread)
 * @note NULL-safe: Returns -1 if client, info_hash, or callback is NULL
 * @note Callback: May be called multiple times
 * 
 * @code
 * void on_peer(void *user_data, const unsigned char *pubkey, uint32_t ip, uint16_t port) {
 *     printf("Found peer at %u.%u.%u.%u:%u\n",
 *            (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
 *            ntohs(port));
 * }
 * 
 * unsigned char info_hash[20];
 * // ... fill info_hash ...
 * 
 * if (norn_discover(client, info_hash, on_peer, NULL) != 0) {
 *     fprintf(stderr, "Discover failed\n");
 * }
 * @endcode
 */
int norn_discover(norn_client_t *client,
                  const unsigned char *info_hash,
                  norn_peer_callback_t callback, void *user_data);

/* === Event loop integration === */

/**
 * @brief Process pending DHT transactions
 * 
 * Must be called regularly (e.g., every 100ms) to process incoming packets,
 * handle timeouts, and invoke callbacks. This is the main event loop hook.
 * 
 * @param client Client handle
 * @return Number of transactions processed, or -1 on error
 * 
 * @note Thread Safety: Not thread-safe
 * @note NULL-safe: Returns -1 if client is NULL
 * @note Non-blocking: Returns quickly; use norn_get_fd() for poll()
 * 
 * @code
 * while (running) {
 *     norn_tick(client);
 *     usleep(100000); // 100ms
 * }
 * @endcode
 */
int norn_tick(norn_client_t *client);

/**
 * @brief Get the socket file descriptor
 * 
 * Returns the UDP socket FD for use with select()/poll()/epoll(). The FD
 * becomes readable when packets are available.
 * 
 * @param client Client handle
 * @return Socket FD, or -1 on error
 * 
 * @note Thread Safety: Thread-safe for reading
 * @note NULL-safe: Returns -1 if client is NULL
 * @note Ownership: FD is owned by client; do not close()
 * 
 * @code
 * int fd = norn_get_fd(client);
 * 
 * fd_set read_fds;
 * FD_ZERO(&read_fds);
 * FD_SET(fd, &read_fds);
 * 
 * struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
 * if (select(fd + 1, &read_fds, NULL, NULL, &timeout) > 0) {
 *     norn_tick(client);
 * }
 * @endcode
 */
int norn_get_fd(const norn_client_t *client);

/* === Record codec === */

/**
 * @brief Encode a mutable record for storage/transmission
 * 
 * Encodes a mutable record to its wire format (BEP-44 mutable item format).
 * 
 * @param rec Record to encode
 * @param out Output buffer
 * @param outcap Capacity of output buffer
 * @return Bytes written, or -1 on error (buffer too small, invalid params)
 * 
 * @note Thread Safety: Thread-safe
 * @note NULL-safe: Returns -1 if rec or out is NULL
 * @note Ownership: out is caller-owned; rec is not modified
 * 
 * @code
 * norn_mutable_t rec;
 * // ... fill rec ...
 * 
 * unsigned char buf[1024];
 * int len = norn_encode_mutable(&rec, buf, sizeof(buf));
 * if (len < 0) {
 *     fprintf(stderr, "Encode failed\n");
 *     return 1;
 * }
 * @endcode
 */
int norn_encode_mutable(const norn_mutable_t *rec,
                         unsigned char *out, size_t outcap);

/**
 * @brief Decode a mutable record from wire format
 * 
 * Decodes a mutable record from its wire format. Verifies the signature.
 * 
 * @param buf Input buffer (wire format)
 * @param len Length of input buffer
 * @param rec Output record structure
 * @return 0 on success, -1 on error (invalid format, bad signature)
 * 
 * @note Thread Safety: Thread-safe
 * @note NULL-safe: Returns -1 if buf or rec is NULL
 * @note Ownership: buf is caller-owned; rec fields are copied
 * 
 * @code
 * unsigned char buf[1024];
 * // ... receive buf from network ...
 * 
 * norn_mutable_t rec;
 * if (norn_decode_mutable(buf, len, &rec) != 0) {
 *     fprintf(stderr, "Decode failed\n");
 *     return 1;
 * }
 * 
 * printf("Value: %.*s\n", (int)rec.value_len, rec.value);
 * @endcode
 */
int norn_decode_mutable(const unsigned char *buf, size_t len,
                         norn_mutable_t *rec);

#endif /* NORN_H */