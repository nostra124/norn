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
    export NORND_BIN="$WORK_DIR/install/bin/nornd"
    # Keep keygen's default ~/.norn under the work dir, and let the installed
    # binary find libnorn.so.
    export HOME="$WORK_DIR"
    export LD_LIBRARY_PATH="$WORK_DIR/install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

    # Start nornd so keygen (and other IPC commands) have a daemon to talk to.
    ssh-keygen -t ed25519 -N "" -C cli-sit -f "$WORK_DIR/id" >/dev/null 2>&1
    export SOCK="$WORK_DIR/nornd.sock"
    export NORN_SOCK="$SOCK"
    "$NORND_BIN" --identity "$WORK_DIR/id" --socket "$SOCK" --class server \
        >"$WORK_DIR/nornd.log" 2>&1 &
    echo $! >"$WORK_DIR/nornd.pid"

    # Wait (≤15s) for nornd to be ready (DNS bootstrap can take a few seconds).
    for _ in $(seq 1 150); do
        if "$NORN_BIN" node status >/dev/null 2>&1; then
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


@test "norn bep44 get (immutable) fails without hash" {
    run "$NORN_BIN" bep44 get
    [ "$status" -eq 1 ]
    [[ "$output" == *"hash"* ]] || [[ "$output" == *"missing"* ]]
}

@test "norn bep44 get (immutable) fails with invalid hex hash" {
    run "$NORN_BIN" bep44 get invalid-hex-hash
    [ "$status" -eq 1 ]
    [[ "$output" == *"hex"* ]] || [[ "$output" == *"invalid"* ]]
}

@test "norn bep44 get (immutable) fails with wrong-length hash" {
    run "$NORN_BIN" bep44 get abc123
    [ "$status" -eq 1 ]
    [[ "$output" == *"40"* ]] || [[ "$output" == *"length"* ]]
}

@test "norn bep44 set fails without name and value" {
    run "$NORN_BIN" bep44 set
    [ "$status" -eq 1 ]
    [[ "$output" == *"name"* ]] || [[ "$output" == *"value"* ]] || [[ "$output" == *"Usage"* ]]
}

@test "norn bep44 put fails without value" {
    run "$NORN_BIN" bep44 put
    [ "$status" -eq 1 ]
    [[ "$output" == *"value"* ]] || [[ "$output" == *"missing"* ]] || [[ "$output" == *"Usage"* ]]
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

@test "norn --port accepts valid port" {
    run "$NORN_BIN" --port 6881 version
    [ "$status" -eq 0 ]
}

@test "norn --timeout accepts valid timeout" {
    run "$NORN_BIN" --timeout 5000 version
    [ "$status" -eq 0 ]
}

@test "norn peer --help shows subcommands" {
    run "$NORN_BIN" peer --help
    [ "$status" -eq 0 ]
    [[ "$output" == *"connect"* ]]
    [[ "$output" == *"get"* ]]
    [[ "$output" == *"cat"* ]]
    [[ "$output" == *"list"* ]]
    [[ "$output" == *"public"* ]]
}

@test "norn peer with no subcommand shows error" {
    run "$NORN_BIN" peer
    [ "$status" -eq 1 ]
    [[ "$output" == *"Usage"* ]]
}

@test "norn peer connect without spec shows usage" {
    run "$NORN_BIN" peer connect
    [ "$status" -eq 1 ]
    [[ "$output" == *"Usage"* ]]
}

@test "norn peer connect with invalid spec fails" {
    run "$NORN_BIN" peer connect dead
    [ "$status" -eq 1 ]
    [[ "$output" == *"norn:"* ]]
}

@test "norn peer get without args shows usage" {
    run "$NORN_BIN" peer get
    [ "$status" -eq 1 ]
    [[ "$output" == *"Usage"* ]]
}

@test "norn peer get with invalid spec fails" {
    run "$NORN_BIN" peer get dead key
    [ "$status" -eq 1 ]
    [[ "$output" == *"norn:"* ]]
}

@test "norn peer cat without args shows usage" {
    run "$NORN_BIN" peer cat
    [ "$status" -eq 1 ]
    [[ "$output" == *"Usage"* ]]
}

@test "norn peer cat with invalid spec fails" {
    run "$NORN_BIN" peer cat dead hash
    [ "$status" -eq 1 ]
    [[ "$output" == *"norn:"* ]]
}

@test "norn peer list returns valid TSV" {
    run "$NORN_BIN" peer list
    [ "$status" -eq 0 ]
    [[ "$output" == *"Node-Id"* ]]
}

@test "norn peer public without node-id shows usage" {
    run "$NORN_BIN" peer public
    [ "$status" -eq 1 ]
    [[ "$output" == *"Usage"* ]]
}

@test "norn peer public with invalid node-id shows error" {
    run "$NORN_BIN" peer public dead
    [ "$status" -eq 2 ]
    [[ "$output" == *"40 hex"* ]]
}

@test "norn peer with unknown subcommand shows error" {
    run "$NORN_BIN" peer nonexistent
    [ "$status" -eq 1 ]
    [[ "$output" == *"Unknown"* ]]
}
