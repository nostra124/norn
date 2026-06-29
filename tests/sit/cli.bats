#!/usr/bin/env bats
# SIT: CLI operations

load test_helper

# Build + install once for the whole file; every test runs the installed CLI.
setup_file() {
    WORK_DIR="$(mktemp -d)"
    export WORK_DIR
    cd "$WORK_DIR"
    copy_src
    autoreconf -fi
    ./configure --prefix="$WORK_DIR/install"
    make
    make install

    export NORN_BIN="$WORK_DIR/install/bin/norn"
    # Keep keygen's default ~/.norn under the work dir, and let the installed
    # binary find libnorn.so.
    export HOME="$WORK_DIR"
    export LD_LIBRARY_PATH="$WORK_DIR/install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
}

teardown_file() {
    rm -rf "$WORK_DIR"
}

# The build is shared across tests (setup_file), but key state is per-test:
# reset it so "no key" tests aren't affected by an earlier keygen.
setup() {
    # Start each test from a clean key state. The default key lives at
    # ~/.config/norn/key.pem; older trees used ~/.norn — clear both.
    rm -rf "$HOME/.config/norn"
    rm -rf "$HOME/.norn"
    rm -f "$WORK_DIR"/*.pem
}

@test "norn --help shows all command groups" {
    run "$NORN_BIN" --help
    [ "$status" -eq 0 ]   # --help is a successful invocation
    [[ "$output" == *"node"* ]]
    [[ "$output" == *"peer"* ]]
    [[ "$output" == *"bep44"* ]]
    [[ "$output" == *"cluster"* ]]
    [[ "$output" == *"version"* ]]
}

@test "norn version prints version" {
    run "$NORN_BIN" version
    [ "$status" -eq 0 ]
    [[ "$output" == *"norn"* ]]
    [[ "$output" =~ [0-9]+\.[0-9]+\.[0-9]+ ]]
}

@test "norn keygen creates key file" {
    # Remove existing key if present
    rm -f "$WORK_DIR/.config/norn/key.pem"

    run "$NORN_BIN" keygen
    [ "$status" -eq 0 ]

    # Key file should exist
    [ -f "$WORK_DIR/.config/norn/key.pem" ]

    # Key file should have correct permissions (0600)
    perms=$(stat -c "%a" "$WORK_DIR/.config/norn/key.pem" 2>/dev/null || stat -f "%OLp" "$WORK_DIR/.config/norn/key.pem")
    [ "$perms" = "600" ]
}

@test "norn keygen prints public key" {
    rm -f "$WORK_DIR/.config/norn/key.pem"

    run "$NORN_BIN" keygen
    [ "$status" -eq 0 ]

    # Should contain hex-encoded public key (64 chars)
    [[ "$output" =~ [0-9a-f]{64} ]]
}

@test "norn keygen fails if key already exists" {
    rm -f "$WORK_DIR/.config/norn/key.pem"

    # First keygen should succeed
    run "$NORN_BIN" keygen
    [ "$status" -eq 0 ]

    # Second keygen should fail
    run "$NORN_BIN" keygen
    [ "$status" -eq 1 ]
    [[ "$output" == *"already exists"* ]]
}

@test "norn keygen respects --key option" {
    run "$NORN_BIN" --key "$WORK_DIR/custom-key.pem" keygen
    [ "$status" -eq 0 ]

    [ -f "$WORK_DIR/custom-key.pem" ]
}

@test "norn keygen respects NORN_KEY environment variable" {
    export NORN_KEY="$WORK_DIR/env-key.pem"

    run "$NORN_BIN" keygen
    [ "$status" -eq 0 ]

    [ -f "$WORK_DIR/env-key.pem" ]
}

@test "norn get fails without key" {
    run "$NORN_BIN" get 0000000000000000000000000000000000000000000000000000000000000000
    [ "$status" -eq 1 ]
    [[ "$output" == *"key"* ]] || [[ "$output" == *"error"* ]]
}

@test "norn get fails with invalid hex key" {
    run "$NORN_BIN" get invalid-hex-key
    [ "$status" -eq 1 ]
    [[ "$output" == *"hex"* ]] || [[ "$output" == *"invalid"* ]]
}

@test "norn get fails with wrong-length key" {
    run "$NORN_BIN" get abc123
    [ "$status" -eq 1 ]
    [[ "$output" == *"64"* ]] || [[ "$output" == *"length"* ]]
}

@test "norn set fails without key" {
    run "$NORN_BIN" set test-value
    [ "$status" -eq 1 ]
    [[ "$output" == *"key"* ]] || [[ "$output" == *"error"* ]]
}

@test "norn set fails without value" {
    "$NORN_BIN" keygen
    run "$NORN_BIN" set
    [ "$status" -eq 1 ]
    [[ "$output" == *"value"* ]] || [[ "$output" == *"Usage"* ]]
}

@test "norn bep44 --help lists get and set" {
    run "$NORN_BIN" bep44 --help
    [ "$status" -eq 0 ]   # --help is a successful invocation
    [[ "$output" == *"get"* ]]
    [[ "$output" == *"set"* ]]
}

@test "norn with invalid command shows error" {
    run "$NORN_BIN" invalid-command
    [ "$status" -eq 1 ]
    [[ "$output" == *"Unknown"* ]] || [[ "$output" == *"invalid"* ]]
}

@test "norn with invalid option shows error" {
    run "$NORN_BIN" --invalid-option version
    [ "$status" -ne 0 ]
}

@test "norn --log-level debug accepts debug" {
    run "$NORN_BIN" --log-level debug version
    [ "$status" -eq 0 ]
}

@test "norn --log-level invalid rejects invalid level" {
    run "$NORN_BIN" --log-level invalid version
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid"* ]] || [[ "$output" == *"invalid"* ]]
}

@test "norn --port accepts valid port" {
    run "$NORN_BIN" --port 6881 version
    [ "$status" -eq 0 ]
}

@test "norn --timeout accepts valid timeout" {
    run "$NORN_BIN" --timeout 5000 version
    [ "$status" -eq 0 ]
}
