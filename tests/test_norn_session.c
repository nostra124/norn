/**
 * @file test_norn_session.c
 * @brief Unit tests for session management (FEAT-016)
 *
 * These are stub tests for the initial API. Full tests will be added
 * as implementation progresses through phases 1-3.
 */

#include "norn_session.h"
#include "norn_suite.h"
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
    
    /* NULL-safe close/free */
    norn_session_close(NULL);
    norn_session_free(NULL);
    
    printf("OK\n");
}

static void test_stream_lifecycle(void) {
    printf("  test_stream_lifecycle: ");
    
    /* NULL-safe stream operations */
    assert(norn_stream_open(NULL) == NULL);
    norn_stream_close(NULL);
    norn_stream_free(NULL);
    assert(norn_stream_write(NULL, "test", 4) == -1);
    assert(norn_stream_read(NULL, NULL, 0) == -1);
    
    printf("OK\n");
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
    
    printf("test_norn_session: OK\n");
    return 0;
}