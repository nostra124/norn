/* SPDX-License-Identifier: MIT */
/**
 * @file norn_relay.h
 * @brief Static relay for NAT traversal (FEAT-017 Phase 4).
 *
 * Provides relay forwarding for peer-to-peer connections when both
 * peers are behind symmetric NAT. This is NOT anonymous routing like Tor.
 * 
 * Design goals:
 * - Static paths advertised in endpoint records
 * - Performance-first (minimal overhead)
 * - Trusted relay nodes (friend nodes)
 * - Multi-hop support (stable path, not rebuilt)
 * - End-to-end encryption (relay cannot read payload)
 * 
 * Multi-Relay Example:
 *   Initiator → Relay1 → Relay2 → Target
 *   - Path discovered dynamically via DHT
 *   - Path stable for session lifetime
 *   - Each relay knows previous/next hop (not anonymous)
 *   - Single encryption (end-to-end), not layered
 */

#ifndef NORN_RELAY_H
#define NORN_RELAY_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations - types defined elsewhere */
#ifndef NORN_CLIENT_T_DEFINED
#define NORN_CLIENT_T_DEFINED
typedef struct norn_client norn_client_t;
#endif
#ifndef NORN_SESSION_T_DEFINED
#define NORN_SESSION_T_DEFINED
typedef struct norn_session norn_session_t;
#endif

#define NORN_RELAY_SESSION_ID_LEN 16
#define NORN_RELAY_CREATE_LEN (1 + 32 + NORN_RELAY_SESSION_ID_LEN + 64)
#define NORN_RELAY_FORWARD_LEN (1 + NORN_RELAY_SESSION_ID_LEN)
#define NORN_RELAY_ACCEPT_LEN (1 + NORN_RELAY_SESSION_ID_LEN + 32 + 64)
#define NORN_RELAY_MAX_PAYLOAD 1400
#define NORN_RELAY_MAX_HOPS 4  /* Maximum relays in path */

#define NORN_MSG_RELAY_CREATE  0x20
#define NORN_MSG_RELAY_FORWARD 0x21
#define NORN_MSG_RELAY_ACCEPT  0x22
#define NORN_MSG_RELAY_CLOSE   0x23

/**
 * @brief Relay create request.
 *
 * Sent to relay to request forwarding to target.
 * For multi-hop, each relay in path forwards to next.
 */
typedef struct {
    uint8_t msg_type;                    /* NORN_MSG_RELAY_CREATE */
    uint8_t target_pubkey[32];          /* Final destination */
    uint8_t session_id[NORN_RELAY_SESSION_ID_LEN]; /* Random session ID */
    uint8_t signature[64];              /* Signed by initiator */
} norn_relay_create_t;

/**
 * @brief Relay forward message.
 *
 * Sent through relay chain to target. Payload is encrypted end-to-end.
 * Each relay forwards to next hop (not decrypted).
 */
typedef struct {
    uint8_t msg_type;                   /* NORN_MSG_RELAY_FORWARD */
    uint8_t session_id[NORN_RELAY_SESSION_ID_LEN];
    uint16_t payload_len;               /* Payload length (supports up to 1400 bytes) */
    uint8_t payload[NORN_RELAY_MAX_PAYLOAD]; /* Encrypted for target */
} norn_relay_forward_t;

/**
 * @brief Relay accept message.
 *
 * Sent by target to accept relay connection.
 * Forwards back through relay chain to initiator.
 */
typedef struct {
    uint8_t msg_type;                   /* NORN_MSG_RELAY_ACCEPT */
    uint8_t session_id[NORN_RELAY_SESSION_ID_LEN];
    uint8_t initiator_pubkey[32];        /* Who we're accepting from */
    uint8_t signature[64];              /* Signed by target */
} norn_relay_accept_t;

/**
 * @brief Relay hint (stored in endpoint payload).
 *
 * Advertises relay nodes that can forward traffic.
 * Multiple hints allow multi-hop paths.
 */
typedef struct {
    uint8_t relay_pubkey[32];            /* Relay's public key */
    uint32_t relay_ip;                   /* Relay's IP (network byte order) */
    uint16_t relay_port;                 /* Relay's port */
} norn_relay_hint_t;

/**
 * @brief Relay path (sequence of relays).
 *
 * Discovered dynamically, stable for session.
 */
typedef struct {
    norn_relay_hint_t hops[NORN_RELAY_MAX_HOPS];
    int hop_count;
    uint8_t session_id[NORN_RELAY_SESSION_ID_LEN];
} norn_relay_path_t;

/**
 * @brief Active relay session (stored in relay state).
 */
typedef struct {
    uint8_t session_id[NORN_RELAY_SESSION_ID_LEN];
    uint8_t initiator_pubkey[32];
    uint8_t target_pubkey[32];
    uint32_t initiator_ip;
    uint16_t initiator_port;
    uint32_t target_ip;
    uint16_t target_port;
    uint64_t created;
    int active;
} norn_relay_session_t;

/**
 * @brief Relay state (for acting as relay).
 */
typedef struct {
    norn_relay_session_t *sessions;
    int session_count;
    int session_cap;
    int enabled;
    void *net; /* net_t* back-pointer for net_send; void* avoids circular include */
} norn_relay_t;

/**
 * @brief Initialize relay state.
 */
int norn_relay_init(norn_relay_t *relay);

/**
 * @brief Cleanup relay state.
 */
void norn_relay_cleanup(norn_relay_t *relay);

/**
 * @brief Encode relay create request.
 */
int norn_encode_relay_create(const norn_relay_create_t *req, uint8_t *out);

/**
 * @brief Decode relay create request.
 */
int norn_decode_relay_create(norn_relay_create_t *out, const uint8_t *in, size_t len);

/**
 * @brief Encode relay forward message.
 */
int norn_encode_relay_forward(const norn_relay_forward_t *msg, uint8_t *out, size_t *out_len);

/**
 * @brief Decode relay forward message.
 */
int norn_decode_relay_forward(norn_relay_forward_t *out, const uint8_t *in, size_t len);

/**
 * @brief Encode relay accept message.
 */
int norn_encode_relay_accept(const norn_relay_accept_t *msg, uint8_t *out);

/**
 * @brief Decode relay accept message.
 */
int norn_decode_relay_accept(norn_relay_accept_t *out, const uint8_t *in, size_t len);

/**
 * @brief Handle relay create request (when acting as relay).
 *
 * Creates relay session and forwards to target.
 *
 * @return 0 on success, -1 on error
 */
int norn_relay_handle_create(norn_relay_t *relay,
                              const norn_relay_create_t *req,
                              uint32_t from_ip,
                              uint16_t from_port,
                              uint8_t *session_id_out);

/**
 * @brief Handle relay forward message (when acting as relay).
 *
 * Forwards payload to target.
 *
 * @return 0 on success, -1 on error
 */
int norn_relay_handle_forward(norn_relay_t *relay,
                               const norn_relay_forward_t *msg,
                               uint32_t from_ip,
                               uint16_t from_port);

/**
 * @brief Get relay session by ID.
 *
 * @return Session pointer or NULL if not found
 */
norn_relay_session_t *norn_relay_find_session(norn_relay_t *relay,
                                               const uint8_t *session_id);

/**
 * @brief Close relay session.
 */
int norn_relay_close_session(norn_relay_t *relay, const uint8_t *session_id);

#endif /* NORN_RELAY_H */