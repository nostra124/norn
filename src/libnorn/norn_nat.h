/**
 * @file norn_nat.h
 * @brief NAT traversal wire protocol (FEAT-017).
 *
 * Defines message formats for:
 * - Hole punch coordination
 * - Relay circuit creation
 * - External IP discovery
 */

#ifndef NORN_NAT_H
#define NORN_NAT_H

#include <stdint.h>
#include <stddef.h>

/* Message types */
#define NORN_MSG_HOLEPUNCH_REQ    0x10
#define NORN_MSG_HOLEPUNCH_RESP   0x11
#define NORN_MSG_RELAY_CREATE     0x20
#define NORN_MSG_RELAY_EXTEND     0x21

/* Hole punch message sizes */
#define NORN_HOLEPUNCH_REQ_LEN    (1 + 32 + 32 + 4 + 2 + 64)  /* 135 bytes */
#define NORN_HOLEPUNCH_RESP_LEN   (1 + 32 + 4 + 2 + 32 + 64)   /* 135 bytes */

/**
 * @brief Hole punch request message.
 *
 * Sent to rendezvous to request coordination with target peer.
 */
typedef struct {
    uint8_t msg_type;              /* NORN_MSG_HOLEPUNCH_REQ */
    uint8_t target_pubkey[32];      /* Who to connect to */
    uint8_t my_ephemeral_pubkey[32]; /* Our ephemeral key for this session */
    uint32_t my_external_ip;        /* Our external IP (from DHT/previous) */
    uint16_t my_external_port;      /* Our external port */
    uint8_t signature[64];          /* Signed by our identity key */
} norn_holepunch_req_t;

/**
 * @brief Hole punch response message.
 *
 * Sent by rendezvous to both peers after receiving requests from both.
 */
typedef struct {
    uint8_t msg_type;              /* NORN_MSG_HOLEPUNCH_RESP */
    uint8_t peer_pubkey[32];        /* Peer's identity key */
    uint32_t peer_external_ip;      /* Peer's external IP */
    uint16_t peer_external_port;    /* Peer's external port */
    uint8_t peer_ephemeral_pubkey[32]; /* Peer's ephemeral key */
    uint8_t signature[64];          /* Signed by rendezvous */
} norn_holepunch_resp_t;

/**
 * @brief Encode hole punch request.
 *
 * @param req Request structure
 * @param out Output buffer (must be NORN_HOLEPUNCH_REQ_LEN bytes)
 * @return 0 on success, -1 on error
 */
int norn_encode_holepunch_req(const norn_holepunch_req_t *req,
                               uint8_t *out);

/**
 * @brief Decode hole punch request.
 *
 * @param out Output structure
 * @param in Input buffer
 * @param len Buffer length
 * @return 0 on success, -1 on error
 */
int norn_decode_holepunch_req(norn_holepunch_req_t *out,
                               const uint8_t *in,
                               size_t len);

/**
 * @brief Encode hole punch response.
 *
 * @param resp Response structure
 * @param out Output buffer (must be NORN_HOLEPUNCH_RESP_LEN bytes)
 * @return 0 on success, -1 on error
 */
int norn_encode_holepunch_resp(const norn_holepunch_resp_t *resp,
                                uint8_t *out);

/**
 * @brief Decode hole punch response.
 *
 * @param out Output structure
 * @param in Input buffer
 * @param len Buffer length
 * @return 0 on success, -1 on error
 */
int norn_decode_holepunch_resp(norn_holepunch_resp_t *out,
                                const uint8_t *in,
                                size_t len);

#endif /* NORN_NAT_H */