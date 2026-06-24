#!/usr/bin/env bats
# Test direct session handshake (FEAT-016 Phase 1)
#
# This test verifies the complete handshake flow:
# 1. Alice (initiator) creates UDP socket and binds
# 2. Bob (responder) creates UDP socket and binds
# 3. Alice sends INIT to Bob
# 4. Bob receives INIT, sends RESP
# 5. Alice receives RESP, sends CONFIRM
# 6. Bob receives CONFIRM, handshake complete
# 7. Both have verified each other's pubkey

load test_helper

@test "session handshake direct" {
    # Skip if not implemented
    skip "Requires event loop integration - Phase 1 complete, SIT pending"
    
    # This test requires two processes communicating via UDP
    # The blocking handshake functions are ready but need
    # fork() based test harness
    
    # For now, this is a placeholder that documents the test design
    
    # Test design:
    # 1. Create two UDP sockets on localhost ports
    # 2. Generate two keypairs (alice and bob)
    # 3. Fork: parent = initiator, child = responder
    # 4. Parent: norn_session_handshake_initiator()
    # 5. Child: norn_session_handshake_responder()
    # 6. Verify both reach ESTABLISHED state
    # 7. Verify both have each other's pubkey
    # 8. Verify both have matching session keys (rx/tx swapped)
    
    # Implementation requires:
    # - fork() based test harness
    # - Process synchronization (SIGUSR1/2 or pipes)
    # - Timeout handling
    
    true
}

@test "session constants" {
    # Verify handshake message sizes
    [ "$CHANNEL_INIT_LEN" -eq 69 ]
    [ "$CHANNEL_RESP_LEN" -eq 133 ]
    [ "$CHANNEL_CONFIRM_LEN" -eq 109 ]
    
    # Verify overhead
    [ "$CHANNEL_OVERHEAD" -eq 40 ]  # nonce(24) + mac(16)
}

@test "session state transitions" {
    # Verify state enum values
    [ "$NORN_SESSION_CONNECTING" -eq 0 ]
    [ "$NORN_SESSION_ESTABLISHED" -eq 1 ]
    [ "$NORN_SESSION_CLOSING" -eq 2 ]
    [ "$NORN_SESSION_CLOSED" -eq 3 ]
}

@test "session null safety" {
    # All functions should be NULL-safe
    
    # Create a simple C program to test
    cat > "$BATS_TMPDIR/test_null.c" << 'EOF'
#include "norn_session.h"
#include <assert.h>

int main(void) {
    // NULL-safe function calls
    assert(norn_session_get_state(NULL) == NORN_SESSION_CLOSED);
    assert(norn_session_get_peer(NULL, NULL) == -1);
    assert(norn_session_get_suite(NULL) == NULL);
    
    norn_session_close(NULL);
    norn_session_free(NULL);
    
    assert(norn_stream_open(NULL) == NULL);
    norn_stream_close(NULL);
    norn_stream_free(NULL);
    
    return 0;
}
EOF
    
    # Compile and run
    $CC -I"$BATS_TEST_DIRNAME/../src/libnorn" \
        "$BATS_TMPDIR/test_null.c" \
        -L"$BATS_TEST_DIRNAME/../.libs" \
        -lnorn -lsodium \
        -o "$BATS_TMPDIR/test_null"
    
    LD_LIBRARY_PATH="$BATS_TEST_DIRNAME/../.libs" \
        "$BATS_TMPDIR/test_null"
}