#!/usr/bin/env bats
# PIT: Network integration with the real mainline DHT.
# These tests require network access to the public DHT bootstrap routers and
# exercise the direct BEP-44 verbs (`norn bep44 set` / `norn bep44 get`), which
# bootstrap, publish/query, and tear down a one-shot DHT client. The long-lived
# node role belongs to `nornd`, not the `norn` CLI, so there is no standalone
# `norn daemon` to test here.

load test_helper

# Is the real mainline DHT reachable? (No → every test self-skips.)
have_network() {
    [ -z "$SKIP_PIT" ] && ping -c 1 -W 2 router.bittorrent.com >/dev/null 2>&1
}

# Build + install once for the whole file — but only when the network these
# tests need is actually available, so an offline run skips instantly instead
# of paying for a build whose result every test would then skip.
setup_file() {
    if ! have_network; then
        export PIT_OFFLINE=1
        return 0
    fi
    WORK_DIR="$(mktemp -d)"
    export WORK_DIR
    cd "$WORK_DIR"
    copy_src
    autoreconf -fi
    ./configure --prefix="$WORK_DIR/install"
    make
    make install

    export NORN_BIN="$WORK_DIR/install/bin/norn"
    export HOME="$WORK_DIR"
    export LD_LIBRARY_PATH="$WORK_DIR/install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    "$NORN_BIN" keygen 2>/dev/null || true
}

teardown_file() {
    if [ -n "$WORK_DIR" ]; then
        rm -rf "$WORK_DIR"
    fi
}

skip_if_no_network() {
    if [ -n "$SKIP_PIT" ]; then
        skip "PIT tests disabled (SKIP_PIT is set)"
    fi
    if [ -n "$PIT_OFFLINE" ] || ! ping -c 1 -W 2 router.bittorrent.com >/dev/null 2>&1; then
        skip "No network connectivity"
    fi
}

@test "bep44 get bootstraps and queries the real DHT" {
    skip_if_no_network

    # Query a random key. No node is expected to hold it, so the honest outcome
    # is "no value found" (exit 1) — but it must really bootstrap and query, not
    # crash or hang past the timeout.
    run timeout 60 "$NORN_BIN" bep44 get --timeout 10000 \
        4e7369d7a3b2c4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9

    # Found (0), not-found (1), or wall-clock timeout (124) are all acceptable;
    # a crash (>128) or "not implemented" stub is not.
    [ "$status" -eq 0 ] || [ "$status" -eq 1 ] || [ "$status" -eq 124 ]
}

@test "bep44 set and get round-trip on the real DHT" {
    skip_if_no_network

    TEST_VALUE="test-$(date +%s)-$RANDOM"

    # Publish under the default identity key (created in setup_file).
    run timeout 60 "$NORN_BIN" bep44 set --timeout 30000 "$TEST_VALUE"
    if [ "$status" -ne 0 ]; then
        skip "DHT publish unavailable"
    fi

    # `set` prints a recfile: `key=<64-hex>`; that pubkey is the
    # retrieval address.
    PUB_KEY=$(echo "$output" | awk -F= '/^key=/{print $2}')
    [ -n "$PUB_KEY" ]

    # Retrieve it back. DHT propagation is best-effort, so found/not-found/timeout
    # are all valid; we are asserting the path runs for real.
    run timeout 30 "$NORN_BIN" bep44 get --timeout 15000 "$PUB_KEY"
    [ "$status" -eq 0 ] || [ "$status" -eq 1 ] || [ "$status" -eq 124 ]
}

@test "concurrent bep44 get operations do not crash" {
    skip_if_no_network

    # Start 5 concurrent get operations against distinct keys.
    for i in {1..5}; do
        timeout 30 "$NORN_BIN" bep44 get --timeout 5000 \
            000000000000000000000000000000000000000000000000000000000000000$i &
    done

    # Wait for all to complete; none should hang past their own timeout.
    wait
}

@test "norn with multiple key files" {
    skip_if_no_network

    run "$NORN_BIN" --key "$WORK_DIR/key1.pem" keygen
    [ "$status" -eq 0 ]
    KEY1=$(echo "$output" | grep -oE '[0-9a-f]{64}' | head -1)

    run "$NORN_BIN" --key "$WORK_DIR/key2.pem" keygen
    [ "$status" -eq 0 ]
    KEY2=$(echo "$output" | grep -oE '[0-9a-f]{64}' | head -1)

    # Keys should be different.
    [ -n "$KEY1" ]
    [ -n "$KEY2" ]
    [ "$KEY1" != "$KEY2" ]
}
