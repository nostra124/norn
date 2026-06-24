/**
 * @file norn_relay.c
 * @brief Static relay for NAT traversal (FEAT-017 Phase 4).
 */

#include "norn_relay.h"
#include <stdlib.h>
#include <string.h>

int norn_relay_init(norn_relay_t *relay) {
    if (!relay) return -1;
    
    relay->session_cap = 16;
    relay->sessions = calloc(relay->session_cap, sizeof(*relay->sessions));
    if (!relay->sessions) return -1;
    
    relay->session_count = 0;
    relay->enabled = 0;
    
    return 0;
}

void norn_relay_cleanup(norn_relay_t *relay) {
    if (!relay) return;
    
    free(relay->sessions);
    relay->sessions = NULL;
    relay->session_count = 0;
    relay->session_cap = 0;
    relay->enabled = 0;
}

int norn_encode_relay_create(const norn_relay_create_t *req, uint8_t *out) {
    if (!req || !out) return -1;
    
    size_t off = 0;
    
    out[off++] = req->msg_type;
    memcpy(out + off, req->target_pubkey, 32);
    off += 32;
    memcpy(out + off, req->session_id, NORN_RELAY_SESSION_ID_LEN);
    off += NORN_RELAY_SESSION_ID_LEN;
    memcpy(out + off, req->signature, 64);
    off += 64;
    
    return (off == NORN_RELAY_CREATE_LEN) ? 0 : -1;
}

int norn_decode_relay_create(norn_relay_create_t *out, const uint8_t *in, size_t len) {
    if (!out || !in) return -1;
    if (len < NORN_RELAY_CREATE_LEN) return -1;
    
    size_t off = 0;
    
    out->msg_type = in[off++];
    memcpy(out->target_pubkey, in + off, 32);
    off += 32;
    memcpy(out->session_id, in + off, NORN_RELAY_SESSION_ID_LEN);
    off += NORN_RELAY_SESSION_ID_LEN;
    memcpy(out->signature, in + off, 64);
    off += 64;
    
    return 0;
}

int norn_encode_relay_forward(const norn_relay_forward_t *msg, uint8_t *out, size_t *out_len) {
    if (!msg || !out || !out_len) return -1;
    
    size_t off = 0;
    
    out[off++] = msg->msg_type;
    memcpy(out + off, msg->session_id, NORN_RELAY_SESSION_ID_LEN);
    off += NORN_RELAY_SESSION_ID_LEN;
    out[off++] = (msg->payload_len >> 8) & 0xFF;
    out[off++] = msg->payload_len & 0xFF;
    memcpy(out + off, msg->payload, msg->payload_len);
    off += msg->payload_len;
    
    *out_len = off;
    return 0;
}

int norn_decode_relay_forward(norn_relay_forward_t *out, const uint8_t *in, size_t len) {
    if (!out || !in) return -1;
    
    size_t min_len = 1 + NORN_RELAY_SESSION_ID_LEN + 2;
    if (len < min_len) return -1;
    
    size_t off = 0;
    
    out->msg_type = in[off++];
    memcpy(out->session_id, in + off, NORN_RELAY_SESSION_ID_LEN);
    off += NORN_RELAY_SESSION_ID_LEN;
    out->payload_len = (in[off] << 8) | in[off + 1];
    off += 2;
    
    if (len < off + out->payload_len) return -1;
    if (out->payload_len > NORN_RELAY_MAX_PAYLOAD) return -1;
    if (out->payload_len == 0) return -1;  /* Empty payload not allowed */
    
    memcpy(out->payload, in + off, out->payload_len);
    
    return 0;
}

int norn_encode_relay_accept(const norn_relay_accept_t *msg, uint8_t *out) {
    if (!msg || !out) return -1;
    
    size_t off = 0;
    
    out[off++] = msg->msg_type;
    memcpy(out + off, msg->session_id, NORN_RELAY_SESSION_ID_LEN);
    off += NORN_RELAY_SESSION_ID_LEN;
    memcpy(out + off, msg->initiator_pubkey, 32);
    off += 32;
    memcpy(out + off, msg->signature, 64);
    off += 64;
    
    return (off == NORN_RELAY_ACCEPT_LEN) ? 0 : -1;
}

int norn_decode_relay_accept(norn_relay_accept_t *out, const uint8_t *in, size_t len) {
    if (!out || !in) return -1;
    if (len < NORN_RELAY_ACCEPT_LEN) return -1;
    
    size_t off = 0;
    
    out->msg_type = in[off++];
    memcpy(out->session_id, in + off, NORN_RELAY_SESSION_ID_LEN);
    off += NORN_RELAY_SESSION_ID_LEN;
    memcpy(out->initiator_pubkey, in + off, 32);
    off += 32;
    memcpy(out->signature, in + off, 64);
    off += 64;
    
    return 0;
}

norn_relay_session_t *norn_relay_find_session(norn_relay_t *relay, const uint8_t *session_id) {
    if (!relay || !session_id) return NULL;
    
    for (int i = 0; i < relay->session_count; i++) {
        if (relay->sessions[i].active &&
            memcmp(relay->sessions[i].session_id, session_id, NORN_RELAY_SESSION_ID_LEN) == 0) {
            return &relay->sessions[i];
        }
    }
    
    return NULL;
}

int norn_relay_handle_create(norn_relay_t *relay,
                              const norn_relay_create_t *req,
                              uint32_t from_ip,
                              uint16_t from_port,
                              uint8_t *session_id_out) {
    if (!relay || !req || !session_id_out) return -1;
    if (!relay->enabled) return -1;
    
    /* Check if session already exists */
    if (norn_relay_find_session(relay, req->session_id)) {
        return -1;
    }
    
    /* Grow sessions array if needed */
    if (relay->session_count >= relay->session_cap) {
        int new_cap = relay->session_cap * 2;
        norn_relay_session_t *new_sessions = realloc(relay->sessions,
                                                       new_cap * sizeof(*new_sessions));
        if (!new_sessions) return -1;
        
        relay->sessions = new_sessions;
        relay->session_cap = new_cap;
    }
    
    /* Create new session */
    norn_relay_session_t *session = &relay->sessions[relay->session_count++];
    memset(session, 0, sizeof(*session));
    
    memcpy(session->session_id, req->session_id, NORN_RELAY_SESSION_ID_LEN);
    memcpy(session->initiator_pubkey, req->session_id, 32); /* Will be filled from signature */
    memcpy(session->target_pubkey, req->target_pubkey, 32);
    session->initiator_ip = from_ip;
    session->initiator_port = from_port;
    session->target_ip = 0; /* Will be filled when target accepts */
    session->target_port = 0;
    session->created = 0; /* TODO: timestamp */
    session->active = 0; /* Becomes active when target accepts */
    
    memcpy(session_id_out, req->session_id, NORN_RELAY_SESSION_ID_LEN);
    
    return 0;
}

int norn_relay_handle_forward(norn_relay_t *relay,
                               const norn_relay_forward_t *msg,
                               uint32_t from_ip,
                               uint16_t from_port) {
    if (!relay || !msg) return -1;
    
    norn_relay_session_t *session = norn_relay_find_session(relay, msg->session_id);
    if (!session) return -1;
    if (!session->active) return -1;
    
    /* Determine direction */
    int from_initiator = (from_ip == session->initiator_ip && 
                          from_port == session->initiator_port);
    
    /* TODO: Forward to the other party */
    /* This will be implemented when integrating with the network layer */
    
    (void)from_initiator;
    
    return 0;
}

int norn_relay_close_session(norn_relay_t *relay, const uint8_t *session_id) {
    if (!relay || !session_id) return -1;
    
    norn_relay_session_t *session = norn_relay_find_session(relay, session_id);
    if (!session) return -1;
    
    session->active = 0;
    
    /* Compact sessions array */
    int idx = session - relay->sessions;
    if (idx < relay->session_count - 1) {
        memmove(&relay->sessions[idx], &relay->sessions[idx + 1],
                (relay->session_count - idx - 1) * sizeof(*relay->sessions));
    }
    relay->session_count--;
    
    return 0;
}