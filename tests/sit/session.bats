#!/usr/bin/env bats
# Test session state machine (FEAT-016 Phase 1)
#
# Tests the async session API without network.

load test_helper

@test "session state transitions" {
    cat > "$BATS_TMPDIR/test_states.c" << 'EOF'
#include <stdio.h>
#include "norn_session.h"

int main(void) {
    printf("NORN_SESSION_RESOLVING=%d\n", NORN_SESSION_RESOLVING);
    printf("NORN_SESSION_CONNECTING=%d\n", NORN_SESSION_CONNECTING);
    printf("NORN_SESSION_ESTABLISHED=%d\n", NORN_SESSION_ESTABLISHED);
    printf("NORN_SESSION_CLOSING=%d\n", NORN_SESSION_CLOSING);
    printf("NORN_SESSION_CLOSED=%d\n", NORN_SESSION_CLOSED);
    
    // Verify expected values (new async states)
    if (NORN_SESSION_RESOLVING != 0) return 1;
    if (NORN_SESSION_CONNECTING != 1) return 2;
    if (NORN_SESSION_ESTABLISHED != 2) return 3;
    if (NORN_SESSION_CLOSING != 3) return 4;
    if (NORN_SESSION_CLOSED != 4) return 5;
    
    return 0;
}
EOF
    
    $CC -I"$BATS_TEST_DIRNAME/../../src/libnorn" \
        "$BATS_TMPDIR/test_states.c" \
        -L"$BATS_TEST_DIRNAME/../../.libs" \
        -lnorn -lsodium \
        -o "$BATS_TMPDIR/test_states"
    
    LD_LIBRARY_PATH="$BATS_TEST_DIRNAME/../../.libs" \
    DYLD_LIBRARY_PATH="$BATS_TEST_DIRNAME/../../.libs" \
        "$BATS_TMPDIR/test_states"
}

@test "session null safety" {
    cat > "$BATS_TMPDIR/test_null.c" << 'EOF'
#include "norn_session.h"
#include <assert.h>

int main(void) {
    // NULL-safe function calls
    assert(norn_session_get_state(NULL) == NORN_SESSION_CLOSED);
    assert(norn_session_get_peer(NULL, NULL) == -1);
    assert(norn_session_get_suite(NULL) == NULL);
    assert(norn_session_get_fd(NULL) == -1);
    
    norn_session_free(NULL);
    
    assert(norn_stream_open_async(NULL, NULL, NULL) == NULL);
    
    return 0;
}
EOF
    
    $CC -I"$BATS_TEST_DIRNAME/../../src/libnorn" \
        "$BATS_TMPDIR/test_null.c" \
        -L"$BATS_TEST_DIRNAME/../../.libs" \
        -lnorn -lsodium \
        -o "$BATS_TMPDIR/test_null"
    
    LD_LIBRARY_PATH="$BATS_TEST_DIRNAME/../../.libs" \
    DYLD_LIBRARY_PATH="$BATS_TEST_DIRNAME/../../.libs" \
        "$BATS_TMPDIR/test_null"
}