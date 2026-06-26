#!/usr/bin/env bats
# SIT: Build and install norn from a clean source tree.
#
# The full pipeline (autoreconf -> configure -> make -> make install) runs once
# in setup_file; its success already proves each stage returns 0 (a failure
# aborts setup_file and surfaces on every test). Each test then asserts one
# concrete outcome against the built/installed tree.

load test_helper

setup_file() {
    WORK_DIR="$(mktemp -d)"
    export WORK_DIR
    cd "$WORK_DIR"
    copy_src
    autoreconf -fi
    ./configure --prefix="$WORK_DIR/install" --enable-static --enable-shared
    make
    make install
    export NORN_BIN="$WORK_DIR/install/bin/norn"
    export LD_LIBRARY_PATH="$WORK_DIR/install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
}

teardown_file() {
    rm -rf "$WORK_DIR"
}

@test "autotools configured the tree" {
    [ -x "$WORK_DIR/configure" ]
    [ -f "$WORK_DIR/config.status" ]
    [ -f "$WORK_DIR/Makefile" ]
}

@test "make built the static and shared libraries" {
    [ -f "$WORK_DIR/.libs/libnorn.a" ]
    ls "$WORK_DIR"/.libs/libnorn.so* >/dev/null 2>&1 || \
        ls "$WORK_DIR"/.libs/libnorn.dylib* >/dev/null 2>&1
}

@test "make built the binaries" {
    [ -x "$WORK_DIR/.libs/norn" ] || [ -x "$WORK_DIR/norn" ]
    [ -x "$WORK_DIR/.libs/nornd" ] || [ -x "$WORK_DIR/nornd" ]
}

@test "make check passes" {
    cd "$WORK_DIR"
    run make check
    [ "$status" -eq 0 ]
    [[ "$output" == *"PASS"* ]] || [[ "$output" == *"# FAIL:  0"* ]]
}

@test "make install placed binaries, library and headers" {
    [ -f "$WORK_DIR/install/bin/norn" ]
    [ -f "$WORK_DIR/install/bin/nornd" ]
    [ -f "$WORK_DIR/install/lib/libnorn.la" ]
    [ -f "$WORK_DIR/install/include/norn.h" ]
}

@test "make install placed the service units" {
    [ -f "$WORK_DIR/install/lib/systemd/system/nornd.service" ]
    [ -f "$WORK_DIR/install/lib/systemd/user/nornd.socket" ]
    [ -f "$WORK_DIR/install/lib/launchd/io.norn.nornd.plist" ]
}

@test "norn --help shows usage" {
    run "$NORN_BIN" --help
    [ "$status" -eq 0 ]   # --help is a successful invocation
    [[ "$output" == *"Usage"* ]]
    [[ "$output" == *"Commands"* ]]
}

@test "norn version prints version" {
    run "$NORN_BIN" version
    [ "$status" -eq 0 ]
    [[ "$output" == *"norn"* ]]
}

@test "pkg-config files are installed" {
    [ -f "$WORK_DIR/install/lib/pkgconfig/norn.pc" ]
    # The .pc lives under the test prefix, so point pkg-config at it.
    PKG_CONFIG_PATH="$WORK_DIR/install/lib/pkgconfig" \
        run pkg-config --cflags norn
    [ "$status" -eq 0 ]
}
