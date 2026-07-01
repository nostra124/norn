/* SPDX-License-Identifier: MIT */
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
#include <time.h>

/** @brief DHT node ID size (SHA-1 hash, 20 bytes) */
#define NORN_ID_BYTES      20

/** @brief Ed25519 public key size (32 bytes) */
#define NORN_PUBKEY_BYTES  32

/** @brief Ed25519 secret key size (64 bytes) */
#define NORN_SECRETKEY_BYTES 64

/* Opaque handles */
#ifndef NORN_CLIENT_T_DEFINED
#define NORN_CLIENT_T_DEFINED
typedef struct norn_client norn_client_t;
#endif
typedef struct norn_record norn_record_t;

/**
 * @brief Application-protocol id multiplexed over a single norn session.
 *
 * A norn node runs several application protocols (cluster consensus, node-served
 * KV, tunnels, consumer apps) concurrently over ONE UDP port and ONE session.
 * Every stream and datagram is tagged with a `norn_service_t` so the session
 * routes it to the right protocol handler without collision — a "port number"
 * scoped to the encrypted session, not the socket. The low range is reserved for
 * libnorn/nornd; consumers allocate from NORN_SVC_USER_BASE upward.
 */
typedef uint16_t norn_service_t;
#define NORN_SVC_DEFAULT    0x0000  /* legacy single-protocol streams (tunnels) */
#define NORN_SVC_RAFT       0x0001  /* nornd cluster consensus frames */
#define NORN_SVC_SERVED_KV  0x0002  /* nornd node-served KV (GET/CAT/LIST) */
#define NORN_SVC_TUNNEL     0x0003  /* norn-forward stream tunnel */
#define NORN_SVC_USER_BASE  0x0100  /* consumers allocate service ids from here */

/** Max distinct services multiplexed concurrently over one session. */
#define NORN_MAX_SERVICES   8

/**
 * @brief Client configuration
 * 
 * Passed to norn_new() to configure the DHT client. All pointers are borrowed
 * (not copied) and must remain valid for the lifetime of the client.
 */
typedef struct {
    const char *version;                  /**< Application version string (e.g., "norn/1.0") */
    int read_only;                        /**< BEP-43: read-only mode (don't respond to queries) */
    int private_mode;                      /**< Bootstrap only to boot_* peers (no public DHT) */
    
    const uint32_t *boot_ips;              /**< Bootstrap peer IPs (network byte order), NULL for default */
    const uint16_t *boot_ports;            /**< Bootstrap peer ports (network byte order), NULL for default */
    int boot_count;                        /**< Number of bootstrap peers */
    
    uint16_t local_port;                   /**< Local nornd port for bootstrap (0 = default 6881) */
    
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
 * @brief Signer for an external identity keystore (e.g. ssh-agent).
 *
 * Produces the 64-byte ed25519 signature over `msg`. `ud` is caller context.
 * Returns 0 on success, non-zero on failure.
 */
typedef int (*norn_sign_fn)(void *ud, unsigned char sig[64],
                            const unsigned char *msg, size_t msglen);

/**
 * @brief Delegate session-handshake signing to an external keystore.
 *
 * By default a client signs its session handshakes with the secret key passed
 * to norn_new(). Installing a signer routes signing through `fn` instead, so the
 * raw ed25519 key never needs to live in this process — it can stay in ssh-agent
 * or an HSM. The public key from norn_new() remains the node identity (peers
 * verify against it), so when a signer is set the `self_sec` given to norn_new()
 * may be a placeholder. Pass fn=NULL to revert to the built-in secret signer.
 * Applies to sessions opened after the call.
 *
 * @param client Client handle (NULL-safe: does nothing)
 * @param fn     Signer callback, or NULL to use the built-in secret signer
 * @param ud     Opaque context passed to every `fn` invocation
 */
void norn_set_signer(norn_client_t *client, norn_sign_fn fn, void *ud);

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

/**
 * @brief Get the node's discovered external (public) IP and port
 *
 * Returns the reflexive endpoint learned from BEP-42 ("ip" field in peer
 * replies). Before any peer has reported it, *have is set to 0 and the
 * out values are left unchanged.
 *
 * @param client Client handle
 * @param ip_out   Filled with the external IP (network byte order) if *have
 * @param port_out Filled with the external port (host byte order) if *have
 * @param have     Set to 1 if an external address has been discovered, 0 if not
 * @return 0 on success, -1 on error (NULL client/outputs)
 *
 * @note NULL-safe: Returns -1 if client, ip_out, port_out, or have is NULL
 */
int norn_external_addr(const norn_client_t *client, uint32_t *ip_out,
                       uint16_t *port_out, int *have);

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
 * @brief Store a salted mutable signed record in the DHT (BEP-44)
 *
 * As norn_put_mutable(), but the target is salted: target = SHA1("k" ‖ pubkey
 * ‖ salt). This lets one keypair publish many distinct named records (the salt
 * is the name), rather than exactly one per key.
 *
 * @param client  Client handle
 * @param pubkey  Ed25519 public key (32 bytes, caller-owned)
 * @param secret  Ed25519 secret key (64 bytes, caller-owned)
 * @param value   Value bytes (max 1000)
 * @param value_len Value length
 * @param seq     Monotonically increasing sequence number
 * @param salt    Salt bytes (the record "name"); may be NULL (unsalted)
 * @param saltlen Salt length
 * @return 0 on success (query sent), -1 on error
 *
 * @note BEP-44: Implements salted mutable items
 * @note NULL-safe: Returns -1 if client, pubkey, secret, or value is NULL
 */
int norn_put_mutable_salt(norn_client_t *client,
                          const unsigned char *pubkey, const unsigned char *secret,
                          const unsigned char *value, size_t value_len,
                          uint32_t seq, const unsigned char *salt, size_t saltlen);

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
 * @brief Retrieve a salted mutable signed record from the DHT (BEP-44)
 *
 * As norn_get_mutable(), but the target is salted: target = SHA1("k" ‖ pubkey
 * ‖ salt). Use this to fetch a record previously stored with
 * norn_put_mutable_salt() under the same pubkey + salt.
 *
 * @param client  Client handle
 * @param pubkey  Ed25519 public key (32 bytes) — the record's owner
 * @param salt    Salt bytes (the record "name"); may be NULL (unsalted)
 * @param saltlen Salt length
 * @param callback Function to call when value is found
 * @param user_data User data passed to callback
 * @return 0 on success (query sent), -1 on error
 *
 * @note BEP-44: Implements salted mutable items
 */
int norn_get_mutable_salt(norn_client_t *client,
                          const unsigned char *pubkey,
                          const unsigned char *salt, size_t saltlen,
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

/**
 * @brief Resolve a node's endpoint and Ed25519 pubkey by DHT node id
 *
 * Performs a get_peers lookup on info_hash = node_id (norn nodes announce
 * under their node id). On success, fills *ip_out, *port_out, and — if the
 * peer published the norn `pk` extension — *pubkey_out (32 bytes). The pubkey
 * is needed to dial an authenticated norn session.
 *
 * @param client   Client handle
 * @param node_id  20-byte DHT node id to resolve
 * @param ip_out       Filled with the peer IP (network byte order) on success
 * @param port_out     Filled with the peer port (host byte order) on success
 * @param pubkey_out   Filled with the 32-byte Ed25519 pubkey on success (may be
 *                     left untouched if the peer didn't publish one)
 * @param timeout_ms   Lookup timeout
 * @return 1 if found, 0 if not found, -1 on error
 *
 * @note NULL-safe: returns -1 if client or node_id is NULL
 */
int norn_resolve_node(norn_client_t *client, const unsigned char *node_id,
                      uint32_t *ip_out, uint16_t *port_out,
                      unsigned char *pubkey_out, int timeout_ms);

/**
 * @brief Look up a node's endpoint in the local DHT routing table
 *
 * Returns the ip/port the routing table holds for `node_id`, without any
 * network round-trip. Used to dial a known peer directly (e.g. for served-KV):
 * the caller dials the endpoint, learns the peer's Ed25519 pubkey from the
 * session handshake, and verifies `bep44_target_for_pubkey(pubkey) == node_id`.
 *
 * @param client  Client handle
 * @param node_id 20-byte DHT node id
 * @param ip_out    Filled with the peer IP (network byte order) if found
 * @param port_out  Filled with the peer port (host byte order) if found
 * @return 1 if found in the local routing table, 0 if not, -1 on error
 *
 * @note NULL-safe: returns -1 if client, node_id, ip_out, or port_out is NULL
 */
int norn_routing_lookup(const norn_client_t *client, const unsigned char *node_id,
                        uint32_t *ip_out, uint16_t *port_out);

/**
 * @brief Look up a peer's Ed25519 public key by DHT node id
 *
 * Returns the 32-byte Ed25519 pubkey the peer advertised (via the norn "pk"
 * extension) during DHT contact, if it is in the local routing table and known
 * to speak norn. Vanilla Mainline DHT nodes don't send `pk`, so this returns 0
 * for them (the node-id is a one-way hash of the pubkey and can't be reversed).
 *
 * @param client   Client handle
 * @param node_id  20-byte DHT node id
 * @param pubkey_out  Filled with the 32-byte Ed25519 pubkey on success
 * @return 1 if found and known, 0 if not in the table / pubkey unknown, -1 on error
 *
 * @note NULL-safe: returns -1 if client, node_id, or pubkey_out is NULL
 */
int norn_routing_pubkey(const norn_client_t *client, const unsigned char *node_id,
                        unsigned char *pubkey_out);

/**
 * @brief Info about a DHT record this node is storing on behalf of the network
 *
 * The DHT is distributed: each node holds the records whose target hash falls
 * close to the node's own id. norn_dht_list() enumerates those locally-held
 * records so `norn bep44 list` can show what this node is storing.
 */
typedef struct {
    unsigned char target[20];  /**< DHT key (SHA1) the record is stored under */
    int           immutable;   /**< 1 = immutable (content-addressed), 0 = mutable */
    size_t        vlen;        /**< Value length in bytes */
    uint32_t      seq;         /**< Sequence number (mutable only; 0 for immutable) */
    long          stored;      /**< Unix timestamp when the record was stored */
} norn_dht_item_t;

/**
 * @brief Enumerate the DHT records this node is holding
 *
 * @param want_immutable 1 to list immutable items, 0 for mutable items
 * @param out   Output array (caller-owned)
 * @param max   Max entries `out` can hold
 * @return Number of entries written (0..max), or -1 on error (NULL out)
 *
 * @note NULL-safe: returns -1 if out is NULL
 */
int norn_dht_list(int want_immutable, norn_dht_item_t *out, int max);

/**
 * @brief Fetch the value of a DHT record this node is holding
 *
 * @param target  20-byte DHT key
 * @param out     Output buffer for the value
 * @param cap     Buffer capacity
 * @return Value length on success, -1 if not held / error
 */
int norn_dht_get_value(const unsigned char *target, unsigned char *out, size_t cap);

/**
 * @brief Get full metadata + value of a held DHT record (for persistence)
 *
 * @param target       20-byte DHT key
 * @param pubkey_out   32-byte Ed25519 pubkey (mutable; zeroed for immutable)
 * @param seq_out      sequence number (mutable; 0 for immutable)
 * @param val_out      value buffer
 * @param vcap         buffer capacity
 * @param vlen_out     actual value length
 * @param sig_out      64-byte signature (mutable; zeroed for immutable)
 * @param immutable_out 1 if immutable, 0 if mutable
 * @return 1 if found, 0 if not found, -1 on error
 */
int norn_dht_get_full(const unsigned char *target, unsigned char *pubkey_out,
                      uint32_t *seq_out, unsigned char *val_out, size_t vcap,
                      size_t *vlen_out, unsigned char *sig_out, int *immutable_out);

/**
 * @brief Restore a mutable record into the local DHT store (on restart)
 *
 * @return 0 on success, -1 on error
 */
int norn_dht_restore_mutable(const unsigned char *target, const unsigned char *pubkey,
                             uint32_t seq, const unsigned char *value, size_t vlen,
                             const unsigned char *sig);

/**
 * @brief Restore an immutable record into the local DHT store (on restart)
 *
 * @return 0 on success, -1 on error
 */
int norn_dht_restore_immutable(const unsigned char *value, size_t vlen);

/**
 * @brief Delete a DHT record this node is holding
 *
 * @param target  20-byte DHT key
 * @return 0 on success, -1 if not found / error
 */
int norn_dht_del(const unsigned char *target);

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

/* === DHT routing table === */

/**
 * @brief A single DHT routing-table node, for norn_routing_nodes().
 *
 * @note id is a 20-byte Mainline DHT node id (SHA-1 of the peer key),
 *       not an Ed25519 pubkey. ip/port are in network byte order.
 */
typedef struct {
    unsigned char id[20];
    uint32_t ip;        /* network byte order */
    uint16_t port;      /* network byte order */
    time_t last_seen;   /* unix seconds of last contact */
    char pv[8];          /* peer's norn (protocol) version, major.minor; "" if non-norn/unknown */
    char app[24];        /* peer's application name, from BEP-5 "v" (e.g. "norn-node", "Transmission"); "" if unknown */
} norn_routing_node_t;

/**
 * @brief Get the number of nodes in the DHT routing table
 *
 * Returns the count of Kademlia nodes currently known in the
 * routing table. This is the number of DHT peers we've discovered.
 *
 * @param client Client handle
 * @return Node count, or -1 on error
 *
 * @note NULL-safe: Returns -1 if client is NULL
 */
int norn_routing_size(const norn_client_t *client);

/**
 * @brief Snapshot the DHT routing table into a caller buffer
 *
 * Copies up to `cap` routing-table nodes into `out`, in table order.
 * Use norn_routing_size() to size the buffer. The snapshot is a point-in-time
 * copy; the table may change after this call.
 *
 * @param client  Client handle
 * @param out     Output array (caller-owned); may be NULL only if cap is 0
 * @param cap     Max entries `out` can hold
 * @return Number of entries written (0..cap), or -1 on error
 *
 * @note NULL-safe: Returns -1 if client or out is NULL
 */
int norn_routing_nodes(const norn_client_t *client, norn_routing_node_t *out,
                       int cap);

/* === DHT state persistence === */

/**
 * @brief Save the DHT routing table to a binary file
 *
 * Persists the known good Kademlia nodes so they can be reloaded on
 * restart, avoiding a full re-bootstrap. Format: magic(4) + version(4)
 * + count(4) + {id(20) + ip(4) + port(2)}*count.
 *
 * @param client Client handle
 * @param path   File path to write to
 * @return Number of nodes saved, or -1 on error
 *
 * @note NULL-safe: Returns -1 if client or path is NULL
 */
int norn_save_dht_nodes(norn_client_t *client, const char *path);

/**
 * @brief Load the DHT routing table from a binary file
 *
 * Restores nodes previously saved with norn_save_dht_nodes(). The loaded
 * nodes are merged into the client's routing table up to MAINLINE_MAX_NODES.
 *
 * @param client Client handle
 * @param path   File path to read from
 * @return Number of nodes loaded, or -1 on error
 *
 * @note NULL-safe: Returns -1 if client or path is NULL
 */
int norn_load_dht_nodes(norn_client_t *client, const char *path);

/**
 * @brief Save the peer cache to a binary file
 *
 * Persists the endpoint cache (account->ip:port mappings) for warm
 * restart. Format: magic(4) + version(4) + count(4) + {key(20) + ip(4)
 * + port(2) + timestamp(8)}*count.
 *
 * @param client Client handle
 * @param path   File path to write to
 * @return Number of entries saved, or -1 on error
 *
 * @note NULL-safe: Returns -1 if client or path is NULL
 */
int norn_save_peer_cache(norn_client_t *client, const char *path);

/**
 * @brief Load the peer cache from a binary file
 *
 * Restores entries previously saved with norn_save_peer_cache().
 *
 * @param client Client handle
 * @param path   File path to read from
 * @return Number of entries loaded, or -1 on error
 *
 * @note NULL-safe: Returns -1 if client or path is NULL
 */
int norn_load_peer_cache(norn_client_t *client, const char *path);

#endif /* NORN_H */