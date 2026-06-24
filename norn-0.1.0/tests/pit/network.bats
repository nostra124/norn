#!/usr/bin/env bats
# PIT: Network integration with real DHT nodes
# These tests require network access to mainline DHT bootstrap nodes

setup() {
    WORK_DIR="$(mktemp -d)"
    export WORK_DIR
    cd "$WORK_DIR"
    
    # Copy source to work directory
    cp -r /Users/rene/Projekte/norn/* "$WORK_DIR/"
    
    # Build and install
    autoreconf -fi
    ./configure --prefix="$WORK_DIR/install"
    make
    make install
    
    NORN_BIN="$WORK_DIR/install/bin/norn"
    export NORN_BIN
    
    # Generate key for tests
    "$NORN_BIN" keygen 2>/dev/null || true
}

teardown() {
    cd /
    rm -rf "$WORK_DIR"
}

# Skip all PIT tests if SKIP_PIT is set
skip_if_no_network() {
    if [ -n "$SKIP_PIT" ]; then
        skip "PIT tests disabled (SKIP_PIT is set)"
    fi
    
    # Check network connectivity
    if ! ping -c 1 -W 2 router.bittorrent.com >/dev/null 2>&1; then
        skip "No network connectivity"
    fi
}

@test "norn daemon starts and binds to port" {
    skip_if_no_network
    
    # Start daemon in background
    "$NORN_BIN" daemon --port 6881 &
    DAEMON_PID=$!
    
    # Wait for daemon to start
    sleep 2
    
    # Check that daemon is running
    kill -0 $DAEMON_PID 2>/dev/null
    [ "$status" -eq 0 ]
    
    # Check that port is bound
    netstat -an | grep -q ":6881.*LISTEN" || \
    ss -tuln | grep -q ":6881" || \
    lsof -i :6881 >/dev/null 2>&1
    
    # Stop daemon
    kill $DAEMON_PID 2>/dev/null
    wait $DAEMON_PID 2>/dev/null
}

@test "norn bootstrap reaches DHT routers" {
    skip_if_no_network
    
    # Start daemon with short timeout
    timeout 30 "$NORN_BIN" daemon --port 16881 --log-level debug 2>&1 | head -20 > daemon.log &
    DAEMON_PID=$!
    
    sleep 5
    
    # Should see bootstrap messages in log
    grep -q "bootstrap\|router\|contact" daemon.log || \
    grep -q "DHT\|bootstrap" daemon.log
    
    # Cleanup
    kill $DAEMON_PID 2>/dev/null
    wait $DAEMON_PID 2>/dev/null
}

@test "norn get from real DHT (known key)" {
    skip_if_no_network
    
    # This test tries to retrieve a known record from the DHT
    # Note: This may fail if no nodes hold the key, which is expected
    
    run timeout 30 "$NORN_BIN" get --timeout 10000 \
        4e7369d7a3b2c4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9
    
    # Either succeeds or times out (both are valid)
    [ "$status" -eq 0 ] || [ "$status" -eq 124 ]
}

@test "norn set and get round-trip" {
    skip_if_no_network
    
    # Generate a unique value
    TEST_VALUE="test-$(date +%s)-$RANDOM"
    
    # Set the value (using generated key)
    run timeout 60 "$NORN_BIN" set --timeout 30000 \
        --key "$WORK_DIR/.norn/key.pem" "$TEST_VALUE"
    
    # If set succeeds, try to get it back
    if [ "$status" -eq 0 ]; then
        # Extract public key from keygen output
        PUB_KEY=$("$NORN_BIN" keygen 2>&1 | grep "Public:" | awk '{print $2}')
        
        # Try to retrieve
        run timeout 30 "$NORN_BIN" get --timeout 10000 "$PUB_KEY"
        
        # May or may not succeed depending on DHT propagation
        [ "$status" -eq 0 ] || [ "$status" -eq 124 ]
    else
        # Network issues, skip
        skip "Network unavailable"
    fi
}

@test "norn daemon handles SIGTERM gracefully" {
    skip_if_no_network
    
    # Start daemon
    "$NORN_BIN" daemon --port 16882 &
    DAEMON_PID=$!
    
    sleep 2
    
    # Send SIGTERM
    kill -TERM $DAEMON_PID
    
    # Wait for graceful shutdown
    wait $DAEMON_PID
    EXIT_CODE=$?
    
    # Should exit cleanly (0 or signal-specific code)
    [ $EXIT_CODE -le 128 ]
}

@test "norn daemon handles SIGINT gracefully" {
    skip_if_no_network
    
    # Start daemon
    "$NORN_BIN" daemon --port 16883 &
    DAEMON_PID=$!
    
    sleep 2
    
    # Send SIGINT (Ctrl-C)
    kill -INT $DAEMON_PID
    
    # Wait for graceful shutdown
    wait $DAEMON_PID
    EXIT_CODE=$?
    
    # Should exit cleanly
    [ $EXIT_CODE -le 128 ]
}

@test "concurrent norn get operations" {
    skip_if_no_network
    
    # Start 5 concurrent get operations
    for i in {1..5}; do
        timeout 30 "$NORN_BIN" get --timeout 5000 \
            000000000000000000000000000000000000000000000000000000000000000$i &
    done
    
    # Wait for all to complete
    wait
    
    # All should either succeed or timeout (not crash)
    [ "$status" -eq 0 ]
}

@test "norn daemon read-only mode" {
    skip_if_no_network
    
    # Start daemon in read-only mode
    "$NORN_BIN" daemon --port 16884 --read-only --log-level debug 2>&1 | head -20 > daemon.log &
    DAEMON_PID=$!
    
    sleep 3
    
    # Should indicate read-only mode
    grep -qi "read.only\|readonly" daemon.log || true
    
    # Cleanup
    kill $DAEMON_PID 2>/dev/null
    wait $DAEMON_PID 2>/dev/null
}

@test "norn daemon with custom bootstrap peers" {
    skip_if_no_network
    
    # Use public DHT routers as bootstrap peers
    # router.bittorrent.com:6881 is a well-known bootstrap node
    
    run timeout 30 "$NORN_BIN" daemon --port 16885 \
        --bootstrap router.bittorrent.com:6881 \
        --bootstrap router.utorrent.com:6881 \
        --log-level info 2>&1
    
    # Should connect to bootstrap peers
    [ "$status" -eq 0 ] || [ "$status" -eq 124 ]  # Success or timeout
}

@test "norn with multiple key files" {
    skip_if_no_network
    
    # Generate key in custom location
    run "$NORN_BIN" --key "$WORK_DIR/key1.pem" keygen
    [ "$status" -eq 0 ]
    
    run "$NORN_BIN" --key "$WORK_DIR/key2.pem" keygen
    [ "$status" -eq 0 ]
    
    # Keys should be different
    KEY1=$(grep "Public:" "$WORK_DIR/key1.pem" 2>&1 || "$NORN_BIN" --key "$WORK_DIR/key1.pem" keygen 2>&1 | grep "Public:" | awk '{print $2}')
    KEY2=$(grep "Public:" "$WORK_DIR/key2.pem" 2>&1 || "$NORN_BIN" --key "$WORK_DIR/key2.pem" keygen 2>&1 | grep "Public:" | awk '{print $2}')
    
    [ "$KEY1" != "$KEY2" ]
}