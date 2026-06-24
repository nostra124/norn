/**
 * @file norn_session.h
 * @brief Session management — connect by public key (FEAT-016).
 *
 * Provides a high-level API for establishing verified, encrypted sessions
 * with peers identified only by their public key. The dial flow:
 *
 * 1. Resolve endpoint via DHT (BEP-44 mutable record)
 * 2. NAT traversal (direct → hole-punch → relay)
 * 3. Channel handshake (ECDH + Ed25519 auth)
 * 4. Session established (verified peer pubkey)
 *
 * Multiple logical streams can be multiplexed over one session.
 *
 * @section thread_safety Thread Safety
 * All functions are single-threaded. Caller must synchronize if using from
 * multiple threads.
 *
 * @section memory Memory Management
 * - Sessions are allocated internally and freed with norn_session_close()
 * - All buffers are caller-owned unless explicitly documented
 */

#ifndef NORN_SESSION_H
#define NORN_SESSION_H

#include "norn.h"
#include "norn_suite.h"
#include <stdint.h>
#include <stddef.h>

/* Opaque handles */
typedef struct norn_session norn_session_t;
typedef struct norn_stream norn_stream_t;

/* Forward declarations */
typedef struct norn_endpoint norn_endpoint_t;

/**
 * @brief Endpoint record (stored in DHT, announced via norn_announce_endpoint)
 */
typedef struct norn_endpoint {
    unsigned char pubkey[32];       /**< Ed25519 or secp256k1 public key */
    uint32_t ip;                     /**< Public IP (network byte order, 0 if behind NAT) */
    uint16_t port;                   /**< Public port (network byte order) */
    unsigned char payload[1024];     /**< Application-specific data (capabilities, etc.) */
    size_t payload_len;              /**< Actual payload length */
} norn_endpoint_t;

/**
 * @brief Direct connection endpoint (for testing without DHT)
 */
typedef struct {
    uint32_t ip;                     /**< IPv4 address (network byte order) */
    uint16_t port;                   /**< UDP port (network byte order) */
} norn_direct_endpoint_t;

/**
 * @brief Session state
 */
typedef enum {
    NORN_SESSION_CONNECTING,    /**< Dial in progress */
    NORN_SESSION_ESTABLISHED,   /**< Handshake complete, ready for streams */
    NORN_SESSION_CLOSING,       /**< Close in progress */
    NORN_SESSION_CLOSED         /**< Session terminated */
} norn_session_state_t;

/**
 * @brief Callback for session state changes
 *
 * @param session Session handle
 * @param state New state
 * @param user_data User-provided pointer from norn_dial/norn_accept
 */
typedef void (*norn_session_callback_t)(norn_session_t *session,
                                         norn_session_state_t state,
                                         void *user_data);

/**
 * @brief Callback for inbound sessions
 *
 * @param session New session handle
 * @param user_data User-provided pointer from norn_listen
 */
typedef void (*norn_accept_callback_t)(norn_session_t *session, void *user_data);

/* === Session lifecycle === */

/**
 * @brief Dial a peer by public key
 *
 * Resolves the peer's endpoint via DHT, performs NAT traversal, and
 * establishes an encrypted channel. The callback is invoked when the
 * session is established or fails.
 *
 * @param client Client handle
 * @param pubkey Peer's public key (32 bytes for Ed25519, suite-dependent)
 * @param suite Crypto suite (NULL for default sodium/Ed25519)
 * @param callback State change callback (or NULL for blocking)
 * @param user_data User data passed to callback
 * @return Session handle, or NULL on immediate error
 *
 * @note Async: Returns immediately if callback is non-NULL
 * @note Blocking: Returns established session if callback is NULL
 * @note NULL-safe: Returns NULL if client or pubkey is NULL
 */
norn_session_t *norn_dial(norn_client_t *client,
                          const unsigned char *pubkey,
                          const norn_crypto_suite_t *suite,
                          norn_session_callback_t callback,
                          void *user_data);

/**
 * @brief Dial a peer by direct endpoint (for testing without DHT)
 *
 * Connects directly to a known IP:port without DHT resolution.
 * Useful for testing and private network scenarios.
 *
 * @param client Client handle
 * @param endpoint Direct endpoint (IP + port)
 * @param pubkey Peer's public key (for channel handshake)
 * @param suite Crypto suite (NULL for default)
 * @return Session handle, or NULL on error
 *
 * @note Phase 1: Direct connection only
 */
norn_session_t *norn_dial_direct(norn_client_t *client,
                                  const norn_direct_endpoint_t *endpoint,
                                  const unsigned char *pubkey,
                                  const norn_crypto_suite_t *suite);

/**
 * @brief Listen for inbound connections
 *
 * Advertises this client's endpoint via DHT and begins accepting connections.
 * The callback is invoked for each new inbound session.
 *
 * @param client Client handle
 * @param port Port to listen on (network byte order, 0 for auto)
 * @param suite Crypto suite (NULL for default sodium/Ed25519)
 * @param callback Inbound session callback
 * @param user_data User data passed to callback
 * @return 0 on success, -1 on error
 *
 * @note Async: Callback is invoked for each new session
 * @note NULL-safe: Returns -1 if client or callback is NULL
 */
int norn_listen(norn_client_t *client,
                uint16_t port,
                const norn_crypto_suite_t *suite,
                norn_accept_callback_t callback,
                void *user_data);

/**
 * @brief Accept an inbound session (blocking)
 *
 * Waits for an inbound session and returns it. Only valid after norn_listen().
 *
 * @param client Client handle
 * @return Session handle, or NULL on error/timeout
 *
 * @note Blocking: Returns when session is established or timeout
 * @note NULL-safe: Returns NULL if client is NULL
 */
norn_session_t *norn_accept(norn_client_t *client);

/**
 * @brief Close a session gracefully
 *
 * Sends close notification to peer and releases resources.
 *
 * @param session Session handle (may be NULL)
 */
void norn_session_close(norn_session_t *session);

/**
 * @brief Free a session
 *
 * Releases all resources. Session must be closed first.
 *
 * @param session Session handle (may be NULL)
 */
void norn_session_free(norn_session_t *session);

/* === Session queries === */

/**
 * @brief Get session state
 *
 * @param session Session handle
 * @return Session state, or NORN_SESSION_CLOSED if session is NULL
 */
norn_session_state_t norn_session_get_state(const norn_session_t *session);

/**
 * @brief Get verified peer public key
 *
 * @param session Session handle
 * @param pubkey Output buffer (suite->pubkey_len bytes)
 * @return 0 on success, -1 on error (NULL params, session not established)
 */
int norn_session_get_peer(const norn_session_t *session, unsigned char *pubkey);

/**
 * @brief Get crypto suite
 *
 * @param session Session handle
 * @return Crypto suite, or NULL if session is NULL
 */
const norn_crypto_suite_t *norn_session_get_suite(const norn_session_t *session);

/* === Stream multiplexing === */

/**
 * @brief Open a logical stream over the session
 *
 * Creates a new reliable, ordered byte stream multiplexed over the session.
 *
 * @param session Session handle
 * @return Stream handle, or NULL on error
 */
norn_stream_t *norn_stream_open(norn_session_t *session);

/**
 * @brief Write data to stream
 *
 * @param stream Stream handle
 * @param data Data to write
 * @param len Length of data
 * @return Bytes written (may be < len if buffer full), or -1 on error
 */
int norn_stream_write(norn_stream_t *stream, const void *data, size_t len);

/**
 * @brief Read data from stream
 *
 * @param stream Stream handle
 * @param buf Output buffer
 * @param cap Buffer capacity
 * @return Bytes read, or -1 on error
 */
int norn_stream_read(norn_stream_t *stream, void *buf, size_t cap);

/**
 * @brief Close stream gracefully
 *
 * Sends FIN and waits for peer's FIN.
 *
 * @param stream Stream handle
 */
void norn_stream_close(norn_stream_t *stream);

/**
 * @brief Free stream
 *
 * Releases resources. Stream must be closed first.
 *
 * @param stream Stream handle (may be NULL)
 */
void norn_stream_free(norn_stream_t *stream);

/* === Endpoint discovery === */

/**
 * @brief Announce endpoint via DHT
 *
 * Publishes this client's endpoint record to the DHT so peers can dial us.
 *
 * @param client Client handle
 * @param endpoint Endpoint to announce
 * @param secret Secret key for signing (suite->secret_len bytes)
 * @param suite Crypto suite (NULL for default)
 * @return 0 on success, -1 on error
 */
int norn_announce_endpoint(norn_client_t *client,
                           const norn_endpoint_t *endpoint,
                           const unsigned char *secret,
                           const norn_crypto_suite_t *suite);

/**
 * @brief Resolve peer endpoint from DHT
 *
 * Looks up a peer's endpoint record in the DHT.
 *
 * @param client Client handle
 * @param pubkey Peer's public key
 * @param endpoint Output endpoint
 * @param suite Crypto suite (NULL for default)
 * @return 0 on success, -1 on error or not found
 */
int norn_resolve_endpoint(norn_client_t *client,
                          const unsigned char *pubkey,
                          norn_endpoint_t *endpoint,
                          const norn_crypto_suite_t *suite);

#endif /* NORN_SESSION_H */