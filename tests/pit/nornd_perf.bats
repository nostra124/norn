#!/usr/bin/env bats
# PIT: nornd cluster KV performance over the IPC socket.
#
# Local (no external network): drives a put/get workload through the `norn`
# CLI against a single-node `nornd` and checks throughput/latency bounds. Each
# operation is a full CLI process + Unix-socket round-trip, so the bounds are
# deliberately generous — the point is to catch gross regressions (hangs,
# pathological slowdowns), not to microbenchmark.

load test_helper

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

    ssh-keygen -t ed25519 -N "" -C nornd-pit -f "$WORK_DIR/id" >/dev/null 2>&1
    export SOCK="$WORK_DIR/nornd.sock"
    export NORN_SOCK="$SOCK"

    "$NORND" --identity "$WORK_DIR/id" --socket "$SOCK" --class server \
        >"$WORK_DIR/nornd.log" 2>&1 &
    echo $! >"$WORK_DIR/nornd.pid"
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

@test "cluster KV sustains a put/get workload and stays correct" {
    N=200
    start=$(date +%s%N)
    for i in $(seq 1 "$N"); do
        "$NORN" cluster put "k$i" "v$i" >/dev/null
    done
    mid=$(date +%s%N)
    # Verify every value round-tripped.
    for i in $(seq 1 "$N"); do
        run "$NORN" cluster get "k$i"
        [ "$status" -eq 0 ]
        [ "$output" = "v$i" ]
    done
    end=$(date +%s%N)

    put_ms=$(( (mid - start) / 1000000 ))
    get_ms=$(( (end - mid) / 1000000 ))
    echo "# ${N} puts in ${put_ms}ms, ${N} verified gets in ${get_ms}ms" >&3

    # Generous ceilings: catch hangs / pathological slowdowns, not jitter.
    [ "$put_ms" -lt 60000 ]
    [ "$get_ms" -lt 60000 ]
}

@test "single cluster put/get round-trip latency is bounded" {
    "$NORN" cluster put lat hello >/dev/null
    start=$(date +%s%N)
    run "$NORN" cluster get lat
    end=$(date +%s%N)
    [ "$status" -eq 0 ]
    [ "$output" = "hello" ]
    ms=$(( (end - start) / 1000000 ))
    echo "# single get round-trip: ${ms}ms" >&3
    [ "$ms" -lt 2000 ]
}

@test "overwriting a key many times keeps the latest value" {
    for i in $(seq 1 50); do
        "$NORN" cluster put churn "val$i" >/dev/null
    done
    run "$NORN" cluster get churn
    [ "$status" -eq 0 ]
    [ "$output" = "val50" ]
}
