/* SPDX-License-Identifier: MIT */
/**
 * @file test_relay.c
 * @brief Test relay wire protocol (FEAT-017 Phase 4).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "norn_relay.h"

int main(void) {
    printf("Testing relay initialization...\n");
    
    norn_relay_t relay;
    int rc = norn_relay_init(&relay);
    assert(rc == 0);
    assert(relay.sessions != NULL);
    assert(relay.session_count == 0);
    assert(relay.session_cap == 16);
    assert(relay.enabled == 0);
    
    printf("Testing relay cleanup...\n");
    norn_relay_cleanup(&relay);
    assert(relay.sessions == NULL);
    
    printf("Testing NULL handling...\n");
    assert(norn_relay_init(NULL) == -1);
    norn_relay_cleanup(NULL);
    
    printf("Testing relay create encode/decode...\n");
    norn_relay_create_t create_req;
    memset(&create_req, 0, sizeof(create_req));
    create_req.msg_type = NORN_MSG_RELAY_CREATE;
    memset(create_req.target_pubkey, 0xAB, 32);
    memset(create_req.session_id, 0xCD, NORN_RELAY_SESSION_ID_LEN);
    memset(create_req.signature, 0xEF, 64);
    
    uint8_t buf[NORN_RELAY_CREATE_LEN];
    rc = norn_encode_relay_create(&create_req, buf);
    assert(rc == 0);
    
    norn_relay_create_t decoded;
    rc = norn_decode_relay_create(&decoded, buf, NORN_RELAY_CREATE_LEN);
    assert(rc == 0);
    assert(decoded.msg_type == NORN_MSG_RELAY_CREATE);
    assert(memcmp(decoded.target_pubkey, create_req.target_pubkey, 32) == 0);
    assert(memcmp(decoded.session_id, create_req.session_id, NORN_RELAY_SESSION_ID_LEN) == 0);
    assert(memcmp(decoded.signature, create_req.signature, 64) == 0);
    
    printf("Testing relay forward encode/decode...\n");
    norn_relay_forward_t forward;
    memset(&forward, 0, sizeof(forward));
    forward.msg_type = NORN_MSG_RELAY_FORWARD;
    memset(forward.session_id, 0x12, NORN_RELAY_SESSION_ID_LEN);
    forward.payload_len = 100;
    memset(forward.payload, 0x34, forward.payload_len);
    
    uint8_t fwd_buf[2048];
    size_t fwd_len;
    rc = norn_encode_relay_forward(&forward, fwd_buf, &fwd_len);
    assert(rc == 0);
    
    norn_relay_forward_t fwd_decoded;
    rc = norn_decode_relay_forward(&fwd_decoded, fwd_buf, fwd_len);
    assert(rc == 0);
    assert(fwd_decoded.msg_type == NORN_MSG_RELAY_FORWARD);
    assert(memcmp(fwd_decoded.session_id, forward.session_id, NORN_RELAY_SESSION_ID_LEN) == 0);
    assert(fwd_decoded.payload_len == forward.payload_len);
    
    printf("Testing relay accept encode/decode...\n");
    norn_relay_accept_t accept;
    memset(&accept, 0, sizeof(accept));
    accept.msg_type = NORN_MSG_RELAY_ACCEPT;
    memset(accept.session_id, 0x56, NORN_RELAY_SESSION_ID_LEN);
    memset(accept.initiator_pubkey, 0x78, 32);
    memset(accept.signature, 0x9A, 64);
    
    uint8_t acc_buf[NORN_RELAY_ACCEPT_LEN];
    rc = norn_encode_relay_accept(&accept, acc_buf);
    assert(rc == 0);
    
    norn_relay_accept_t acc_decoded;
    rc = norn_decode_relay_accept(&acc_decoded, acc_buf, NORN_RELAY_ACCEPT_LEN);
    assert(rc == 0);
    assert(acc_decoded.msg_type == NORN_MSG_RELAY_ACCEPT);
    assert(memcmp(acc_decoded.session_id, accept.session_id, NORN_RELAY_SESSION_ID_LEN) == 0);
    assert(memcmp(acc_decoded.initiator_pubkey, accept.initiator_pubkey, 32) == 0);
    assert(memcmp(acc_decoded.signature, accept.signature, 64) == 0);
    
    printf("Testing relay session management...\n");
    norn_relay_init(&relay);
    
    /* Find non-existent session */
    uint8_t session_id[NORN_RELAY_SESSION_ID_LEN];
    memset(session_id, 0xFF, NORN_RELAY_SESSION_ID_LEN);
    norn_relay_session_t *session = norn_relay_find_session(&relay, session_id);
    assert(session == NULL);
    
    /* Close non-existent session */
    rc = norn_relay_close_session(&relay, session_id);
    assert(rc == -1);
    
    norn_relay_cleanup(&relay);
    
    printf("All relay tests passed!\n");
    return 0;
}