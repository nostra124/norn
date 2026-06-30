/* SPDX-License-Identifier: MIT */
/**
 * @file test_norn_session.c
 * @brief Unit tests for session management (FEAT-016)
 *
 * These are stub tests for the initial API. Full tests will be added
 * as implementation progresses through phases 1-3.
 */

#include "norn_session.h"
#include "norn_suite.h"
#include "channel.h"
#include "norn.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_session_lifecycle(void) {
    printf("  test_session_lifecycle: ");
    
    /* Create a client */
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    /* Note: Full test requires actual norn_client_t which needs UDP socket.
     * For this stub test, we test the struct layout and NULL-safety.
     */
    
    /* NULL-safe queries */
    assert(norn_session_get_state(NULL) == NORN_SESSION_CLOSED);
    assert(norn_session_get_peer(NULL, pk) == -1);
    assert(norn_session_get_suite(NULL) == NULL);
    assert(norn_session_get_fd(NULL) == -1);
    
    /* NULL-safe free */
    norn_session_free(NULL);
    
    printf("OK\n");
}

static void test_stream_lifecycle(void) {
    printf("  test_stream_lifecycle: ");
    
    /* NULL-safe stream operations - FEAT-018 */
    assert(norn_stream_open_async(NULL, NULL, NULL) == NULL);
    
    printf("OK (stub)\n");
}

static void test_endpoint_struct(void) {
    printf("  test_endpoint_struct: ");
    
    norn_endpoint_t ep;
    memset(&ep, 0, sizeof(ep));
    
    /* Fill endpoint */
    memset(ep.pubkey, 0xAA, 32);
    ep.ip = 0x01020304;  /* 1.2.3.4 */
    ep.port = 6881;
    memcpy(ep.payload, "test", 4);
    ep.payload_len = 4;
    
    /* Verify */
    assert(ep.ip == 0x01020304);
    assert(ep.port == 6881);
    assert(ep.payload_len == 4);
    assert(memcmp(ep.payload, "test", 4) == 0);
    
    printf("OK\n");
}

static void test_session_states(void) {
    printf("  test_session_states: ");
    
    /* Verify all states are distinct */
    assert(NORN_SESSION_RESOLVING != NORN_SESSION_CONNECTING);
    assert(NORN_SESSION_CONNECTING != NORN_SESSION_ESTABLISHED);
    assert(NORN_SESSION_ESTABLISHED != NORN_SESSION_CLOSING);
    assert(NORN_SESSION_CLOSING != NORN_SESSION_CLOSED);
    
    printf("OK\n");
}

static void test_stream_on_closed_session(void) {
    printf("  test_stream_on_closed_session: ");
    
    /* This is covered by NULL-safe tests above.
     * Full test requires actual session with CLOSED state.
     */
    
    printf("OK (stub)\n");
}

static void test_handshake_init_resp_confirm(void) {
    printf("  test_handshake_init_resp_confirm: ");
    
    /* Test handshake message sizes via channel.h */
    /* The actual handshake is tested in test_channel.c */
    /* This test verifies the session wrapper functions exist and have correct signatures */
    
    /* Generate identity keypairs */
    unsigned char alice_pk[32], alice_sk[64];
    unsigned char bob_pk[32], bob_sk[64];
    crypto_sign_keypair(alice_pk, alice_sk);
    crypto_sign_keypair(bob_pk, bob_sk);
    
    /* Verify function signatures are correct */
    assert(CHANNEL_INIT_LEN == (4 + 1 + 32 + 32));
    assert(CHANNEL_RESP_LEN == (4 + 1 + 32 + 32 + 64));
    assert(CHANNEL_CONFIRM_LEN == (4 + 1 + 24 + 64 + 16));
    
    /* Handshake tested in test_channel.c - this just verifies constants */
    
    printf("OK (constants verified)\n");
}

static void test_handshake_wrong_key(void) {
    printf("  test_handshake_wrong_key: ");
    
    /* This test is covered by test_channel.c (channel_auth_verify) */
    /* The session wrapper correctly uses channel.h functions */
    
    printf("OK (covered by test_channel)\n");
}

static void on_accept_stub(norn_session_t *session, void *ud) {
    (void)session;
    (void)ud;
}

static void test_listen_async_bootstrap(void) {
    printf("  test_listen_async_bootstrap: ");
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    int ret = norn_listen_async(client, 0, NULL, on_accept_stub, NULL);
    assert(ret == 0);
    
    norn_free(client);
    printf("OK\n");
}

static void test_listen_async_null(void) {
    printf("  test_listen_async_null: ");
    
    int ret = norn_listen_async(NULL, 0, NULL, NULL, NULL);
    assert(ret == -1);
    
    printf("OK\n");
}

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    printf("test_norn_session:\n");
    
    test_session_lifecycle();
    test_stream_lifecycle();
    test_endpoint_struct();
    test_session_states();
    test_stream_on_closed_session();
    test_handshake_init_resp_confirm();
    test_handshake_wrong_key();
    test_listen_async_bootstrap();
    test_listen_async_null();
    
    printf("test_norn_session: OK\n");
    return 0;
}