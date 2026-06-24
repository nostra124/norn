/**
 * @file norn_nat.h
 * @brief NAT traversal wire protocol (FEAT-017).
 *
 * Defines message formats for:
 * - Hole punch coordination
 * - Relay circuit creation
 * - STUN-like external IP discovery
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
#define NORN_MSG_STUN_REQ         0x30
#define NORN_MSG_STUN_RESP        0x31

/* Hole punch message sizes */
#define NORN_HOLEPUNCH_REQ_LEN    (1 + 32 + 32 + 4 + 2 + 64)  /* 135 bytes */
#define NORN_HOLEPUNCH_RESP_LEN   (1 + 32 + 4 + 2 + 32 + 64)   /* 135 bytes */
#define NORN_STUN_REQ_LEN        (1 + 32)                      /* 33 bytes */
#define NORN_STUN_RESP_LEN       (1 + 32 + 4 + 2)               /* 39 bytes */

/**
 * @brief Hole punch request message.
 *
 * Sent to rendezvous to request coordination with target peer.
 */
typedef struct {
    uint8_t msg_type;              /* NORN_MSG_HOLEPUNCH_REQ */
    uint8_t target_pubkey[32];      /* Who to connect to */
    uint8_t my_ephemeral_pubkey[32]; /* Our ephemeral key for this session */
    uint32_t my_external_ip;        /* Our external IP (from STUN) */
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
 * @brief STUN request message.
 *
 * Sent to STUN server to discover external IP/port.
 */
typedef struct {
    uint8_t msg_type;              /* NORN_MSG_STUN_REQ */
    uint8_t transaction_id[32];     /* Random transaction ID */
} norn_stun_req_t;

/**
 * @brief STUN response message.
 *
 * Returned by STUN server with external IP/port.
 */
typedef struct {
    uint8_t msg_type;              /* NORN_MSG_STUN_RESP */
    uint8_t transaction_id[32];     /* Matches request */
    uint32_t external_ip;           /* Our external IP */
    uint16_t external_port;         /* Our external port */
} norn_stun_resp_t;

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

/**
 * @brief Encode STUN request.
 *
 * @param req Request structure
 * @param out Output buffer (must be NORN_STUN_REQ_LEN bytes)
 * @return 0 on success, -1 on error
 */
int norn_encode_stun_req(const norn_stun_req_t *req,
                         uint8_t *out);

/**
 * @brief Decode STUN request.
 *
 * @param out Output structure
 * @param in Input buffer
 * @param len Buffer length
 * @return 0 on success, -1 on error
 */
int norn_decode_stun_req(norn_stun_req_t *out,
                         const uint8_t *in,
                         size_t len);

/**
 * @brief Encode STUN response.
 *
 * @param resp Response structure
 * @param out Output buffer (must be NORN_STUN_RESP_LEN bytes)
 * @return 0 on success, -1 on error
 */
int norn_encode_stun_resp(const norn_stun_resp_t *resp,
                          uint8_t *out);

/**
 * @brief Decode STUN response.
 *
 * @param out Output structure
 * @param in Input buffer
 * @param len Buffer length
 * @return 0 on success, -1 on error
 */
int norn_decode_stun_resp(norn_stun_resp_t *out,
                          const uint8_t *in,
                          size_t len);

#endif /* NORN_NAT_H */