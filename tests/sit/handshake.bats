#!/usr/bin/env bats
# Test session handshake message flow (FEAT-016 Phase 1)
#
# Tests the handshake message functions without network I/O.
# Network tests require SIT with fork() harness.

load test_helper

@test "session build init" {
    cat > "$BATS_TMPDIR/test_build_init.c" << 'EOF'
#include "norn_session_internal.h"
#include "channel.h"
#include <sodium.h>
#include <stdio.h>
#include <assert.h>

int main(void) {
    if (sodium_init() < 0) return 1;
    
    // Generate identity keypair
    unsigned char pubkey[32], secret[64];
    crypto_sign_keypair(pubkey, secret);
    
    // Create mock initiator session
    norn_session_t *session = calloc(1, sizeof(*session));
    assert(session != NULL);
    session->suite = norn_suite_sodium();
    session->is_initiator = 1;
    session->state = NORN_SESSION_CONNECTING;
    memcpy(session->self_pubkey, pubkey, 32);
    memcpy(session->self_secret, secret, 64);
    
    // Generate ephemeral key
    assert(channel_gen_ephemeral(&session->channel) == 0);
    
    // Build INIT
    unsigned char init_msg[CHANNEL_INIT_LEN];
    int len = norn_session_build_init(session, init_msg, sizeof(init_msg));
    assert(len == CHANNEL_INIT_LEN);
    
    // Verify INIT structure
    assert(init_msg[0] == 0x44);  // 'D'
    assert(init_msg[1] == 0x48);  // 'H'
    assert(init_msg[2] == 0x43);  // 'C'
    assert(init_msg[3] == 0x48);  // 'H'
    assert(init_msg[4] == CHANNEL_MSG_INIT);
    
    free(session);
    printf("OK\n");
    return 0;
}
EOF
    
    $CC -I"$BATS_TEST_DIRNAME/../src/libnorn" \
        "$BATS_TMPDIR/test_build_init.c" \
        -L"$BATS_TEST_DIRNAME/../.libs" \
        -lnorn -lsodium \
        -o "$BATS_TMPDIR/test_build_init"
    
    LD_LIBRARY_PATH="$BATS_TEST_DIRNAME/../.libs" \
        "$BATS_TMPDIR/test_build_init"
}

@test "session accept init" {
    cat > "$BATS_TMPDIR/test_accept_init.c" << 'EOF'
#include "norn_session_internal.h"
#include "channel.h"
#include <sodium.h>
#include <stdio.h>
#include <assert.h>

int main(void) {
    if (sodium_init() < 0) return 1;
    
    // Generate identity keypairs
    unsigned char alice_pk[32], alice_sk[64];
    unsigned char bob_pk[32], bob_sk[64];
    crypto_sign_keypair(alice_pk, alice_sk);
    crypto_sign_keypair(bob_pk, bob_sk);
    
    // Alice: create initiator and build INIT
    norn_session_t *alice = calloc(1, sizeof(*alice));
    assert(alice != NULL);
    alice->suite = norn_suite_sodium();
    alice->is_initiator = 1;
    alice->state = NORN_SESSION_CONNECTING;
    memcpy(alice->self_pubkey, alice_pk, 32);
    memcpy(alice->self_secret, alice_sk, 64);
    channel_gen_ephemeral(&alice->channel);
    
    unsigned char init_msg[CHANNEL_INIT_LEN];
    int init_len = norn_session_build_init(alice, init_msg, sizeof(init_msg));
    assert(init_len > 0);
    
    // Bob: create responder and handle INIT
    norn_session_t *bob = calloc(1, sizeof(*bob));
    assert(bob != NULL);
    bob->suite = norn_suite_sodium();
    bob->is_initiator = 0;
    bob->state = NORN_SESSION_CONNECTING;
    memcpy(bob->self_pubkey, bob_pk, 32);
    memcpy(bob->self_secret, bob_sk, 64);
    channel_gen_ephemeral(&bob->channel);
    
    unsigned char resp_msg[CHANNEL_RESP_LEN];
    int resp_len = norn_session_accept_init(bob, init_msg, init_len,
                                            resp_msg, sizeof(resp_msg));
    assert(resp_len == CHANNEL_RESP_LEN);
    
    // Verify Bob learned Alice's pubkey
    assert(memcmp(bob->peer_pubkey, alice_pk, 32) == 0);
    
    free(alice);
    free(bob);
    printf("OK\n");
    return 0;
}
EOF
    
    $CC -I"$BATS_TEST_DIRNAME/../src/libnorn" \
        "$BATS_TMPDIR/test_accept_init.c" \
        -L"$BATS_TEST_DIRNAME/../.libs" \
        -lnorn -lsodium \
        -o "$BATS_TMPDIR/test_accept_init"
    
    LD_LIBRARY_PATH="$BATS_TEST_DIRNAME/../.libs" \
        "$BATS_TMPDIR/test_accept_init"
}

@test "session complete handshake" {
    cat > "$BATS_TMPDIR/test_complete.c" << 'EOF'
#include "norn_session_internal.h"
#include "channel.h"
#include <sodium.h>
#include <stdio.h>
#include <assert.h>

int main(void) {
    if (sodium_init() < 0) return 1;
    
    // Generate identity keypairs
    unsigned char alice_pk[32], alice_sk[64];
    unsigned char bob_pk[32], bob_sk[64];
    crypto_sign_keypair(alice_pk, alice_sk);
    crypto_sign_keypair(bob_pk, bob_sk);
    
    // Alice: create initiator
    norn_session_t *alice = calloc(1, sizeof(*alice));
    assert(alice != NULL);
    alice->suite = norn_suite_sodium();
    alice->is_initiator = 1;
    alice->state = NORN_SESSION_CONNECTING;
    memcpy(alice->self_pubkey, alice_pk, 32);
    memcpy(alice->self_secret, alice_sk, 64);
    channel_gen_ephemeral(&alice->channel);
    
    // Bob: create responder
    norn_session_t *bob = calloc(1, sizeof(*bob));
    assert(bob != NULL);
    bob->suite = norn_suite_sodium();
    bob->is_initiator = 0;
    bob->state = NORN_SESSION_CONNECTING;
    memcpy(bob->self_pubkey, bob_pk, 32);
    memcpy(bob->self_secret, bob_sk, 64);
    channel_gen_ephemeral(&bob->channel);
    
    // Alice: build INIT
    unsigned char init_msg[CHANNEL_INIT_LEN];
    int init_len = norn_session_build_init(alice, init_msg, sizeof(init_msg));
    assert(init_len > 0);
    
    // Bob: accept INIT, build RESP
    unsigned char resp_msg[CHANNEL_RESP_LEN];
    int resp_len = norn_session_accept_init(bob, init_msg, init_len,
                                            resp_msg, sizeof(resp_msg));
    assert(resp_len > 0);
    
    // Alice: confirm RESP, build CONFIRM
    unsigned char confirm_msg[CHANNEL_CONFIRM_LEN];
    int confirm_len = norn_session_confirm_resp(alice, resp_msg, resp_len,
                                                 confirm_msg, sizeof(confirm_msg));
    assert(confirm_len > 0);
    
    // Alice should now be established
    assert(norn_session_get_state(alice) == NORN_SESSION_ESTABLISHED);
    
    // Bob: finish CONFIRM
    int ret = norn_session_finish_confirm(bob, confirm_msg, confirm_len);
    assert(ret == 0);
    
    // Bob should now be established
    assert(norn_session_get_state(bob) == NORN_SESSION_ESTABLISHED);
    
    // Verify both have each other's pubkey
    assert(memcmp(alice->peer_pubkey, bob_pk, 32) == 0);
    assert(memcmp(bob->peer_pubkey, alice_pk, 32) == 0);
    
    free(alice);
    free(bob);
    printf("OK\n");
    return 0;
}
EOF
    
    $CC -I"$BATS_TEST_DIRNAME/../src/libnorn" \
        "$BATS_TMPDIR/test_complete.c" \
        -L"$BATS_TEST_DIRNAME/../.libs" \
        -lnorn -lsodium \
        -o "$BATS_TMPDIR/test_complete"
    
    LD_LIBRARY_PATH="$BATS_TEST_DIRNAME/../.libs" \
        "$BATS_TMPDIR/test_complete"
}