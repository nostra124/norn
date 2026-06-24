/**
 * @file norn_nat.c
 * @brief NAT traversal wire protocol implementation (FEAT-017).
 */

#include "norn_nat.h"
#include <string.h>

int norn_encode_holepunch_req(const norn_holepunch_req_t *req,
                              uint8_t *out) {
    if (!req || !out) return -1;
    
    size_t off = 0;
    
    /* Message type */
    out[off++] = req->msg_type;
    
    /* Target pubkey */
    memcpy(out + off, req->target_pubkey, 32);
    off += 32;
    
    /* Our ephemeral pubkey */
    memcpy(out + off, req->my_ephemeral_pubkey, 32);
    off += 32;
    
    /* External IP (network byte order) */
    out[off++] = (req->my_external_ip >> 24) & 0xFF;
    out[off++] = (req->my_external_ip >> 16) & 0xFF;
    out[off++] = (req->my_external_ip >> 8) & 0xFF;
    out[off++] = req->my_external_ip & 0xFF;
    
    /* External port (network byte order) */
    out[off++] = (req->my_external_port >> 8) & 0xFF;
    out[off++] = req->my_external_port & 0xFF;
    
    /* Signature */
    memcpy(out + off, req->signature, 64);
    off += 64;
    
    return (off == NORN_HOLEPUNCH_REQ_LEN) ? 0 : -1;
}

int norn_decode_holepunch_req(norn_holepunch_req_t *out,
                              const uint8_t *in,
                              size_t len) {
    if (!out || !in) return -1;
    if (len < NORN_HOLEPUNCH_REQ_LEN) return -1;
    
    size_t off = 0;
    
    /* Message type */
    out->msg_type = in[off++];
    
    /* Target pubkey */
    memcpy(out->target_pubkey, in + off, 32);
    off += 32;
    
    /* Ephemeral pubkey */
    memcpy(out->my_ephemeral_pubkey, in + off, 32);
    off += 32;
    
    /* External IP */
    out->my_external_ip = (in[off] << 24) | (in[off+1] << 16) | 
                           (in[off+2] << 8) | in[off+3];
    off += 4;
    
    /* External port */
    out->my_external_port = (in[off] << 8) | in[off+1];
    off += 2;
    
    /* Signature */
    memcpy(out->signature, in + off, 64);
    off += 64;
    
    return 0;
}

int norn_encode_holepunch_resp(const norn_holepunch_resp_t *resp,
                               uint8_t *out) {
    if (!resp || !out) return -1;
    
    size_t off = 0;
    
    /* Message type */
    out[off++] = resp->msg_type;
    
    /* Peer pubkey */
    memcpy(out + off, resp->peer_pubkey, 32);
    off += 32;
    
    /* Peer external IP */
    out[off++] = (resp->peer_external_ip >> 24) & 0xFF;
    out[off++] = (resp->peer_external_ip >> 16) & 0xFF;
    out[off++] = (resp->peer_external_ip >> 8) & 0xFF;
    out[off++] = resp->peer_external_ip & 0xFF;
    
    /* Peer external port */
    out[off++] = (resp->peer_external_port >> 8) & 0xFF;
    out[off++] = resp->peer_external_port & 0xFF;
    
    /* Peer ephemeral pubkey */
    memcpy(out + off, resp->peer_ephemeral_pubkey, 32);
    off += 32;
    
    /* Signature */
    memcpy(out + off, resp->signature, 64);
    off += 64;
    
    return (off == NORN_HOLEPUNCH_RESP_LEN) ? 0 : -1;
}

int norn_decode_holepunch_resp(norn_holepunch_resp_t *out,
                               const uint8_t *in,
                               size_t len) {
    if (!out || !in) return -1;
    if (len < NORN_HOLEPUNCH_RESP_LEN) return -1;
    
    size_t off = 0;
    
    /* Message type */
    out->msg_type = in[off++];
    
    /* Peer pubkey */
    memcpy(out->peer_pubkey, in + off, 32);
    off += 32;
    
    /* Peer external IP */
    out->peer_external_ip = (in[off] << 24) | (in[off+1] << 16) | 
                            (in[off+2] << 8) | in[off+3];
    off += 4;
    
    /* Peer external port */
    out->peer_external_port = (in[off] << 8) | in[off+1];
    off += 2;
    
    /* Peer ephemeral pubkey */
    memcpy(out->peer_ephemeral_pubkey, in + off, 32);
    off += 32;
    
    /* Signature */
    memcpy(out->signature, in + off, 64);
    off += 64;
    
    return 0;
}

int norn_encode_stun_req(const norn_stun_req_t *req,
                         uint8_t *out) {
    if (!req || !out) return -1;
    
    size_t off = 0;
    
    /* Message type */
    out[off++] = req->msg_type;
    
    /* Transaction ID */
    memcpy(out + off, req->transaction_id, 32);
    off += 32;
    
    return (off == NORN_STUN_REQ_LEN) ? 0 : -1;
}

int norn_decode_stun_req(norn_stun_req_t *out,
                         const uint8_t *in,
                         size_t len) {
    if (!out || !in) return -1;
    if (len < NORN_STUN_REQ_LEN) return -1;
    
    size_t off = 0;
    
    /* Message type */
    out->msg_type = in[off++];
    
    /* Transaction ID */
    memcpy(out->transaction_id, in + off, 32);
    off += 32;
    
    return 0;
}

int norn_encode_stun_resp(const norn_stun_resp_t *resp,
                          uint8_t *out) {
    if (!resp || !out) return -1;
    
    size_t off = 0;
    
    /* Message type */
    out[off++] = resp->msg_type;
    
    /* Transaction ID */
    memcpy(out + off, resp->transaction_id, 32);
    off += 32;
    
    /* External IP */
    out[off++] = (resp->external_ip >> 24) & 0xFF;
    out[off++] = (resp->external_ip >> 16) & 0xFF;
    out[off++] = (resp->external_ip >> 8) & 0xFF;
    out[off++] = resp->external_ip & 0xFF;
    
    /* External port */
    out[off++] = (resp->external_port >> 8) & 0xFF;
    out[off++] = resp->external_port & 0xFF;
    
    return (off == NORN_STUN_RESP_LEN) ? 0 : -1;
}

int norn_decode_stun_resp(norn_stun_resp_t *out,
                          const uint8_t *in,
                          size_t len) {
    if (!out || !in) return -1;
    if (len < NORN_STUN_RESP_LEN) return -1;
    
    size_t off = 0;
    
    /* Message type */
    out->msg_type = in[off++];
    
    /* Transaction ID */
    memcpy(out->transaction_id, in + off, 32);
    off += 32;
    
    /* External IP */
    out->external_ip = (in[off] << 24) | (in[off+1] << 16) | 
                       (in[off+2] << 8) | in[off+3];
    off += 4;
    
    /* External port */
    out->external_port = (in[off] << 8) | in[off+1];
    off += 2;
    
    return 0;
}