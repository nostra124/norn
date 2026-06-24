/**
 * @file norn_session.h
 * @brief Async session management — connect by public key (FEAT-016).
 *
 * Provides a high-level async API for establishing verified, encrypted sessions
 * with peers identified only by their public key. All operations are non-blocking
 * and integrate with event loops via callbacks.
 *
 * @section mobile Mobile Platforms
 * - iOS: Use CFRunLoop integration via norn_get_fd() + CFSocket
 * - Android: Use epoll integration via norn_get_fd() + JNI
 * - libuv: Use norn_get_fd() + uv_poll_init
 *
 * @section lifecycle Session Lifecycle
 * 1. Dial: norn_dial_async() → callback(NORN_SESSION_ESTABLISHED)
 * 2. Use: norn_stream_write() / norn_stream_read() on streams
 * 3. Close: norn_session_close_async() → callback(NORN_SESSION_CLOSED)
 *
 * @section thread_safety Thread Safety
 * All functions are single-threaded. Caller must synchronize if using from
 * multiple threads.
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

/**
 * @brief Session state
 */
typedef enum {
    NORN_SESSION_RESOLVING,    /**< DHT lookup in progress */
    NORN_SESSION_CONNECTING,   /**< Handshake in progress */
    NORN_SESSION_ESTABLISHED,  /**< Ready for streams */
    NORN_SESSION_CLOSING,      /**< Close in progress */
    NORN_SESSION_CLOSED        /**< Session terminated */
} norn_session_state_t;

/**
 * @brief Endpoint record (stored in DHT)
 */
typedef struct {
    unsigned char pubkey[32];       /**< Ed25519 or secp256k1 public key */
    uint32_t ip;                     /**< Public IP (network byte order) */
    uint16_t port;                   /**< Public port (network byte order) */
    unsigned char payload[1024];     /**< Application-specific data */
    size_t payload_len;              /**< Actual payload length */
} norn_endpoint_t;

/**
 * @brief Direct connection endpoint (for testing)
 */
typedef struct {
    uint32_t ip;                     /**< IPv4 address (network byte order) */
    uint16_t port;                   /**< UDP port (network byte order) */
} norn_direct_endpoint_t;

/**
 * @brief Callback for session state changes
 *
 * @param session Session handle
 * @param state New state
 * @param user_data User-provided pointer from norn_dial_async/norn_accept_async
 */
typedef void (*norn_session_callback_t)(norn_session_t *session,
                                        norn_session_state_t state,
                                        void *user_data);

/**
 * @brief Callback for inbound sessions
 *
 * @param session New session handle
 * @param user_data User-provided pointer from norn_listen_async
 */
typedef void (*norn_accept_callback_t)(norn_session_t *session, void *user_data);

/* === Async Session API (Primary) === */

/**
 * @brief Dial a peer by public key (async)
 *
 * Resolves peer endpoint via DHT, performs handshake, and invokes callback
 * when session is established or fails.
 *
 * @param client Client handle
 * @param pubkey Peer's public key (suite->pubkey_len bytes)
 * @param suite Crypto suite (NULL for default sodium/Ed25519)
 * @param callback State change callback
 * @param user_data User data passed to callback
 * @return 0 on success (callback will be invoked), -1 on immediate error
 *
 * @note Non-blocking: Returns immediately
 * @note Callback is invoked from norn_tick()
 * @note NULL-safe: Returns -1 if client or pubkey is NULL
 *
 * @code
 * void on_session(norn_session_t *s, norn_session_state_t state, void *data) {
 *     if (state == NORN_SESSION_ESTABLISHED) {
 *         // Session ready, open streams
 *     }
 * }
 * 
 * norn_dial_async(client, peer_pubkey, NULL, on_session, NULL);
 * 
 * while (running) {
 *     norn_tick(client);
 *     usleep(100000); // 100ms
 * }
 * @endcode
 */
int norn_dial_async(norn_client_t *client,
                    const unsigned char *pubkey,
                    const norn_crypto_suite_t *suite,
                    norn_session_callback_t callback,
                    void *user_data);

/**
 * @brief Dial a peer by direct endpoint (async, for testing)
 *
 * Connects directly to known IP:port without DHT resolution.
 *
 * @param client Client handle
 * @param endpoint Direct endpoint (IP + port)
 * @param pubkey Peer's public key (for handshake)
 * @param suite Crypto suite (NULL for default)
 * @param callback State change callback
 * @param user_data User data passed to callback
 * @return 0 on success, -1 on error
 *
 * @note Non-blocking: Returns immediately
 * @note For testing/private networks only
 */
int norn_dial_direct_async(norn_client_t *client,
                           const norn_direct_endpoint_t *endpoint,
                           const unsigned char *pubkey,
                           const norn_crypto_suite_t *suite,
                           norn_session_callback_t callback,
                           void *user_data);

/**
 * @brief Listen for inbound connections (async)
 *
 * Binds to port and invokes callback for each new session.
 *
 * @param client Client handle
 * @param port Port to listen on (network byte order, 0 for auto)
 * @param suite Crypto suite (NULL for default)
 * @param callback Inbound session callback
 * @param user_data User data passed to callback
 * @return 0 on success, -1 on error
 *
 * @note Non-blocking: Returns immediately
 * @note Callback is invoked from norn_tick()
 */
int norn_listen_async(norn_client_t *client,
                      uint16_t port,
                      const norn_crypto_suite_t *suite,
                      norn_accept_callback_t callback,
                      void *user_data);

/**
 * @brief Close a session gracefully (async)
 *
 * Sends close notification to peer and releases resources.
 * Callback is invoked with NORN_SESSION_CLOSED when complete.
 *
 * @param session Session handle
 * @param callback Close callback (or NULL)
 * @param user_data User data passed to callback
 * @return 0 on success, -1 on error
 */
int norn_session_close_async(norn_session_t *session,
                             norn_session_callback_t callback,
                             void *user_data);

/* === Event Loop Integration === */

/**
 * @brief Process all pending events (non-blocking)
 *
 * Must be called regularly (e.g., every 100ms) to process:
 * - Incoming packets
 * - Timeout handling
 * - Session state transitions
 * - Callback invocations
 *
 * @param client Client handle
 * @return Number of events processed, or -1 on error
 *
 * @note Non-blocking: Returns quickly even if no events
 * @note NULL-safe: Returns -1 if client is NULL
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
 * @brief Get file descriptor for poll()/select()
 *
 * Returns the main DHT socket FD. Use with select()/poll()/epoll/kqueue
 * to wait for events efficiently.
 *
 * @param client Client handle
 * @return Socket FD, or -1 on error
 *
 * @note Use for external event loop integration
 * @code
 * int fd = norn_get_fd(client);
 * while (running) {
 *     fd_set read_fds;
 *     FD_ZERO(&read_fds);
 *     FD_SET(fd, &read_fds);
 *     
 *     struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
 *     if (select(fd + 1, &read_fds, NULL, NULL, &tv) > 0) {
 *         norn_tick(client);
 *     }
 * }
 * @endcode
 */
int norn_get_fd(const norn_client_t *client);

/**
 * @brief Get all session FDs for poll()
 *
 * Fills arrays with all active session file descriptors and their events.
 * Use for external event loop integration.
 *
 * @param client Client handle
 * @param fds Output array for FDs
 * @param events Output array for events (POLLIN, etc.)
 * @param max_fds Maximum number of FDs to return
 * @return Number of FDs filled, or -1 on error
 *
 * @note Caller must allocate fds and events arrays
 */
int norn_get_session_fds(norn_client_t *client,
                         int *fds,
                         int *events,
                         int max_fds);

/* === Session Queries === */

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
 * @param session Session handle (must be ESTABLISHED)
 * @param pubkey Output buffer (suite->pubkey_len bytes)
 * @return 0 on success, -1 on error
 */
int norn_session_get_peer(const norn_session_t *session, unsigned char *pubkey);

/**
 * @brief Get crypto suite
 *
 * @param session Session handle
 * @return Crypto suite, or NULL if session is NULL
 */
const norn_crypto_suite_t *norn_session_get_suite(const norn_session_t *session);

/**
 * @brief Get session socket FD
 *
 * @param session Session handle
 * @return Socket FD, or -1 if session is NULL or closed
 *
 * @note For external event loop integration only
 */
int norn_session_get_fd(const norn_session_t *session);

/* === Stream Multiplexing (Future) === */

/**
 * @brief Open a logical stream (async)
 *
 * Creates a new reliable, ordered byte stream over the session.
 *
 * @param session Session handle
 * @param callback Stream event callback
 * @param user_data User data
 * @return 0 on success, -1 on error
 *
 * @note FEAT-018: Stream multiplexing not yet implemented
 */
int norn_stream_open_async(norn_session_t *session,
                           void *callback,
                           void *user_data);

/* === Blocking API (Deprecated, testing only) === */

/**
 * @brief Perform complete initiator handshake (blocking)
 *
 * @deprecated Use norn_dial_direct_async() instead
 * @note Only for testing. Will be removed in v1.0.0
 */
int norn_session_handshake_initiator(norn_session_t *session,
                                     const unsigned char *self_pub,
                                     const unsigned char *self_secret,
                                     int timeout_ms);

/**
 * @brief Perform complete responder handshake (blocking)
 *
 * @deprecated Use norn_listen_async() instead
 * @note Only for testing. Will be removed in v1.0.0
 */
int norn_session_handshake_responder(norn_session_t *session,
                                     const unsigned char *self_pub,
                                     const unsigned char *self_secret,
                                     int timeout_ms);

/* === Low-Level Handshake (Internal Use) === */

/**
 * @brief Build INIT message (initiator)
 *
 * @note Internal use only. Use norn_dial_async() instead.
 */
int norn_session_build_init(norn_session_t *session,
                            unsigned char *out,
                            size_t outcap);

/**
 * @brief Handle INIT and build RESP (responder)
 *
 * @note Internal use only. Use norn_listen_async() instead.
 */
int norn_session_accept_init(norn_session_t *session,
                             const unsigned char *init_msg,
                             size_t init_len,
                             unsigned char *out,
                             size_t outcap);

/**
 * @brief Handle RESP and build CONFIRM (initiator)
 *
 * @note Internal use only.
 */
int norn_session_confirm_resp(norn_session_t *session,
                              const unsigned char *resp_msg,
                              size_t resp_len,
                              unsigned char *out,
                              size_t outcap);

/**
 * @brief Handle CONFIRM and complete handshake (responder)
 *
 * @note Internal use only.
 */
int norn_session_finish_confirm(norn_session_t *session,
                                const unsigned char *confirm_msg,
                                size_t confirm_len);

/* === Session Resource Management === */

/**
 * @brief Free session resources
 *
 * @param session Session handle (may be NULL)
 */
void norn_session_free(norn_session_t *session);

/* === Internal Functions (for norn_impl.c) === */

/**
 * @brief Register session for event processing (internal)
 *
 * @note Internal use only. Called by norn_dial_async() etc.
 */
int norn_client_add_session(norn_client_t *client, norn_session_t *session);

/**
 * @brief Unregister session from event processing (internal)
 *
 * @note Internal use only.
 */
int norn_client_remove_session(norn_client_t *client, norn_session_t *session);

/**
 * @brief Process pending session events (internal)
 *
 * @note Called by norn_tick()
 */
int norn_client_tick_sessions(norn_client_t *client);

/* === Endpoint Discovery === */

/**
 * @brief Announce endpoint via DHT (async)
 *
 * @param client Client handle
 * @param endpoint Endpoint to announce
 * @param secret Secret key for signing
 * @param suite Crypto suite (NULL for default)
 * @param callback Completion callback
 * @param user_data User data
 * @return 0 on success, -1 on error
 */
int norn_announce_endpoint_async(norn_client_t *client,
                                 const norn_endpoint_t *endpoint,
                                 const unsigned char *secret,
                                 const norn_crypto_suite_t *suite,
                                 void *callback,
                                 void *user_data);

/**
 * @brief Resolve peer endpoint from DHT (async)
 *
 * @param client Client handle
 * @param pubkey Peer's public key
 * @param suite Crypto suite (NULL for default)
 * @param callback Resolution callback
 * @param user_data User data
 * @return 0 on success, -1 on error
 */
int norn_resolve_endpoint_async(norn_client_t *client,
                                const unsigned char *pubkey,
                                const norn_crypto_suite_t *suite,
                                void *callback,
                                void *user_data);

#endif /* NORN_SESSION_H */