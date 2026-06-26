#!/usr/bin/env bats
# SIT: multi-node nornd cluster over real norn sessions (FEAT-029).
#
# Builds and installs norn + nornd once, then starts TWO daemons on loopback
# wired to each other in direct mode (`--peer <pubkey>@127.0.0.1:<port>`). The
# two server nodes form a Raft cluster over real UDP sessions; the tests verify
# leader election, membership, and that the cluster KV replicates both ways
# (writes on the leader are visible on the follower and vice-versa via the
# follower's transparent forward to the leader).

load test_helper

# Raw ed25519 public key (the node id nornd uses) from an OpenSSH `.pub` line:
# the base64 blob decodes to <len><"ssh-ed25519"><len><32-byte key>; take the
# trailing 32 bytes as hex.
node_pubkey() {
    awk '{print $2}' "$1" | base64 -d | od -An -tx1 | tr -d ' \n' | tail -c 64
}

wait_for_leader() {
    # Echoes "A" or "B" once one of the two nodes reports leader; empty on timeout.
    for _ in $(seq 1 100); do
        if NORN_SOCK="$SOCK_A" "$NORN" cluster status 2>/dev/null | grep -q "role: leader"; then
            echo A; return
        fi
        if NORN_SOCK="$SOCK_B" "$NORN" cluster status 2>/dev/null | grep -q "role: leader"; then
            echo B; return
        fi
        sleep 0.2
    done
}

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

    ssh-keygen -t ed25519 -N "" -C nornd-A -f "$WORK_DIR/idA" >/dev/null 2>&1
    ssh-keygen -t ed25519 -N "" -C nornd-B -f "$WORK_DIR/idB" >/dev/null 2>&1
    PKA="$(node_pubkey "$WORK_DIR/idA.pub")"
    PKB="$(node_pubkey "$WORK_DIR/idB.pub")"
    export PORT_A=47811 PORT_B=47812
    export SOCK_A="$WORK_DIR/a.sock" SOCK_B="$WORK_DIR/b.sock"

    "$NORND" --identity "$WORK_DIR/idA" --socket "$SOCK_A" --class server \
        --listen-port "$PORT_A" --peer "${PKB}@127.0.0.1:${PORT_B}" \
        >"$WORK_DIR/a.log" 2>&1 &
    echo $! >"$WORK_DIR/a.pid"
    "$NORND" --identity "$WORK_DIR/idB" --socket "$SOCK_B" --class server \
        --listen-port "$PORT_B" --peer "${PKA}@127.0.0.1:${PORT_A}" \
        >"$WORK_DIR/b.log" 2>&1 &
    echo $! >"$WORK_DIR/b.pid"

    LEADER="$(wait_for_leader)"
    export LEADER
    if [ "$LEADER" = A ]; then
        export LSOCK="$SOCK_A" FSOCK="$SOCK_B"
    else
        export LSOCK="$SOCK_B" FSOCK="$SOCK_A"
    fi
}

teardown_file() {
    [ -f "$WORK_DIR/a.pid" ] && kill "$(cat "$WORK_DIR/a.pid")" 2>/dev/null || true
    [ -f "$WORK_DIR/b.pid" ] && kill "$(cat "$WORK_DIR/b.pid")" 2>/dev/null || true
    rm -rf "$WORK_DIR"
}

@test "two nodes elect a single leader" {
    [ -n "$LEADER" ]
}

@test "both nodes agree on the leader and see two members" {
    la="$(NORN_SOCK="$SOCK_A" "$NORN" cluster leader)"
    lb="$(NORN_SOCK="$SOCK_B" "$NORN" cluster leader)"
    [ -n "$la" ]
    [ "$la" = "$lb" ]
    run bash -c "NORN_SOCK='$SOCK_A' '$NORN' cluster status"
    [[ "$output" == *"members: 2"* ]]
    run bash -c "NORN_SOCK='$SOCK_B' '$NORN' cluster status"
    [[ "$output" == *"members: 2"* ]]
}

@test "a write on the leader replicates to the follower" {
    NORN_SOCK="$LSOCK" "$NORN" cluster put rk1 rv1
    # Allow a heartbeat for the commit to reach and apply on the follower.
    # `|| true`: a not-yet-replicated key exits non-zero — keep polling, don't
    # let bats' errexit abort on the command substitution.
    val=""
    for _ in $(seq 1 25); do
        val="$(NORN_SOCK="$FSOCK" "$NORN" cluster get rk1 2>/dev/null || true)"
        [ "$val" = "rv1" ] && break
        sleep 0.2
    done
    [ "$val" = "rv1" ]
}

@test "a write on the follower is forwarded and replicates to the leader" {
    NORN_SOCK="$FSOCK" "$NORN" cluster put rk2 rv2
    val=""
    for _ in $(seq 1 25); do
        val="$(NORN_SOCK="$LSOCK" "$NORN" cluster get rk2 2>/dev/null || true)"
        [ "$val" = "rv2" ] && break
        sleep 0.2
    done
    [ "$val" = "rv2" ]
}

@test "a delete replicates across the cluster" {
    NORN_SOCK="$LSOCK" "$NORN" cluster put rk3 rv3
    for _ in $(seq 1 25); do
        [ "$(NORN_SOCK="$FSOCK" "$NORN" cluster get rk3 2>/dev/null)" = "rv3" ] && break
        sleep 0.2
    done
    NORN_SOCK="$LSOCK" "$NORN" cluster del rk3
    gone=1
    for _ in $(seq 1 25); do
        if NORN_SOCK="$FSOCK" "$NORN" cluster get rk3 >/dev/null 2>&1; then
            gone=0; sleep 0.2
        else
            gone=1; break
        fi
    done
    [ "$gone" -eq 1 ]
}

@test "an unreachable peer spec is rejected at startup" {
    run "$NORND" --identity "$WORK_DIR/idA" --socket "$WORK_DIR/bad.sock" \
        --peer "not-a-valid-pubkey"
    [ "$status" -eq 2 ]
    [[ "$output" == *"bad --peer"* ]]
}
