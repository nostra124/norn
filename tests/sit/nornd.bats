#!/usr/bin/env bats
# SIT: nornd daemon + norn cluster/keys IPC CLI (v0.12.0)
#
# Builds and installs norn + nornd, starts a single-node daemon keyed on an
# ed25519 SSH identity, and drives the cluster KV + key directory end-to-end
# over the Unix socket. The daemon is built and started once per file.

load test_helper

# Build+install once, then start one single-node nornd and wait for leadership.
setup_file() {
    WORK_DIR="$(mktemp -d)"
    export WORK_DIR
    cd "$WORK_DIR"
    copy_src
    autoreconf -fi >/dev/null 2>&1
    ./configure --prefix="$WORK_DIR/install" >/dev/null 2>&1
    make >/dev/null 2>&1
    make install >/dev/null 2>&1

    export NORN="$WORK_DIR/install/bin/norn"
    export NORND="$WORK_DIR/install/bin/nornd"
    export LD_LIBRARY_PATH="$WORK_DIR/install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

    # ed25519 SSH identity (+ .pub the daemon publishes to the directory).
    ssh-keygen -t ed25519 -N "" -C nornd-sit -f "$WORK_DIR/id" >/dev/null 2>&1

    export SOCK="$WORK_DIR/nornd.sock"
    export NORN_SOCK="$SOCK"

    "$NORND" --identity "$WORK_DIR/id" --socket "$SOCK" --class server \
        >"$WORK_DIR/nornd.log" 2>&1 &
    echo $! >"$WORK_DIR/nornd.pid"

    # Wait (≤5s) for the lone server to elect itself.
    for _ in $(seq 1 50); do
        if "$NORN" cluster status 2>/dev/null | grep -q "role: leader"; then
            break
        fi
        sleep 0.1
    done
}

teardown_file() {
    if [ -f "$WORK_DIR/nornd.pid" ]; then
        kill "$(cat "$WORK_DIR/nornd.pid")" 2>/dev/null || true
    fi
    rm -rf "$WORK_DIR"
}

@test "nornd binary and units are installed" {
    [ -x "$WORK_DIR/install/bin/nornd" ]
    [ -f "$WORK_DIR/install/lib/systemd/system/nornd.service" ]
    [ -f "$WORK_DIR/install/lib/systemd/user/nornd.socket" ]
    [ -f "$WORK_DIR/install/lib/launchd/io.norn.nornd.plist" ]
}

@test "cluster status reports a single-node leader" {
    run "$NORN" cluster status
    [ "$status" -eq 0 ]
    [[ "$output" == *"role: leader"* ]]
    [[ "$output" == *"members: 1"* ]]
}

@test "cluster put/get round-trips a value" {
    run "$NORN" cluster put greet hello
    [ "$status" -eq 0 ]
    run "$NORN" cluster get greet
    [ "$status" -eq 0 ]
    [ "$output" = "hello" ]
}

@test "cluster cas swaps on a matching expect" {
    "$NORN" cluster put k_cas v1
    run "$NORN" cluster cas k_cas v1 v2
    [ "$status" -eq 0 ]
    run "$NORN" cluster get k_cas
    [ "$output" = "v2" ]
}

@test "cluster cas fails on a mismatched expect" {
    "$NORN" cluster put k_mis aaa
    run "$NORN" cluster cas k_mis WRONG bbb
    [ "$status" -eq 1 ]
    [[ "$output" == *"mismatch"* ]]
    run "$NORN" cluster get k_mis
    [ "$output" = "aaa" ]   # unchanged
}

@test "cluster del removes a key" {
    "$NORN" cluster put k_del x
    run "$NORN" cluster del k_del
    [ "$status" -eq 0 ]
    run "$NORN" cluster get k_del
    [ "$status" -eq 1 ]
    [[ "$output" == *"not found"* ]]
}

@test "cluster get of a missing key fails" {
    run "$NORN" cluster get definitely-absent
    [ "$status" -eq 1 ]
    [[ "$output" == *"not found"* ]]
}

@test "cluster members lists the node pubkey" {
    run "$NORN" cluster members
    [ "$status" -eq 0 ]
    [[ "$output" =~ [0-9a-f]{64} ]]
}

@test "cluster leader prints the leader pubkey" {
    run "$NORN" cluster leader
    [ "$status" -eq 0 ]
    [[ "$output" =~ ^[0-9a-f]{64}$ ]]
}

@test "keys resolves the daemon's published SSH key" {
    id="$("$NORN" cluster leader)"
    [ -n "$id" ]
    run "$NORN" keys "$id"
    [ "$status" -eq 0 ]
    [[ "$output" == *"ssh-ed25519"* ]]
}

@test "keys rejects a malformed nodeid" {
    run "$NORN" keys not-a-valid-hex-id
    [ "$status" -eq 2 ]
}

@test "unknown cluster subcommand exits 2" {
    run "$NORN" cluster frobnicate
    [ "$status" -eq 2 ]
    [[ "$output" == *"unknown cluster subcommand"* ]]
}

@test "cluster command fails clearly when nornd is unreachable" {
    NORN_SOCK="$WORK_DIR/nope.sock" run "$NORN" cluster status
    [ "$status" -eq 1 ]
    [[ "$output" == *"nornd"* ]]
}

@test "norn --help lists the cluster and keys verbs" {
    run "$NORN" --help
    [[ "$output" == *"cluster"* ]]
    [[ "$output" == *"keys"* ]]
}
