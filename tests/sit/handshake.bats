#!/usr/bin/env bats
# Test session handshake message flow (FEAT-016 Phase 1)
#
# Tests the handshake message functions via public API.

load test_helper

@test "session constants" {
    cat > "$BATS_TMPDIR/test_constants.c" << 'EOF'
#include <stdio.h>
#include "channel.h"

int main(void) {
    printf("CHANNEL_INIT_LEN=%d\n", CHANNEL_INIT_LEN);
    printf("CHANNEL_RESP_LEN=%d\n", CHANNEL_RESP_LEN);
    printf("CHANNEL_CONFIRM_LEN=%d\n", CHANNEL_CONFIRM_LEN);
    printf("CHANNEL_OVERHEAD=%d\n", CHANNEL_OVERHEAD);
    
    // Verify expected values
    if (CHANNEL_INIT_LEN != 69) return 1;
    if (CHANNEL_RESP_LEN != 133) return 2;
    if (CHANNEL_CONFIRM_LEN != 109) return 3;
    if (CHANNEL_OVERHEAD != 40) return 4;
    
    return 0;
}
EOF
    
    $CC -I"$BATS_TEST_DIRNAME/../../src/libnorn" \
        "$BATS_TMPDIR/test_constants.c" \
        -L"$BATS_TEST_DIRNAME/../../.libs" \
        -lnorn -lsodium \
        -o "$BATS_TMPDIR/test_constants"
    
    LD_LIBRARY_PATH="$BATS_TEST_DIRNAME/../../.libs" \
    DYLD_LIBRARY_PATH="$BATS_TEST_DIRNAME/../../.libs" \
        "$BATS_TMPDIR/test_constants"
}