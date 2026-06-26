#!/usr/bin/env bats
# SIT: Build and install norn in a container

load test_helper

setup() {
    WORK_DIR="$(mktemp -d)"
    export WORK_DIR
    cd "$WORK_DIR"
    
    # Copy source to work directory
    copy_src
}

teardown() {
    cd /
    rm -rf "$WORK_DIR"
}

@test "autoreconf runs successfully" {
    run autoreconf -fi
    [ "$status" -eq 0 ]
}

@test "configure runs successfully" {
    autoreconf -fi
    run ./configure
    [ "$status" -eq 0 ]
}

@test "make builds successfully" {
    autoreconf -fi
    ./configure
    run make
    [ "$status" -eq 0 ]
}

@test "make check passes" {
    autoreconf -fi
    ./configure
    make
    run make check
    [ "$status" -eq 0 ]
}

@test "make install succeeds" {
    autoreconf -fi
    ./configure --prefix="$WORK_DIR/install"
    make
    run make install
    [ "$status" -eq 0 ]
    
    # Verify installed files
    [ -f "$WORK_DIR/install/bin/norn" ]
    [ -f "$WORK_DIR/install/lib/libnorn.la" ]
    [ -f "$WORK_DIR/install/include/norn.h" ]
}

@test "norn --help shows usage" {
    autoreconf -fi
    ./configure --prefix="$WORK_DIR/install"
    make
    make install
    
    run "$WORK_DIR/install/bin/norn" --help
    [ "$status" -eq 0 ]   # --help is a successful invocation
    [[ "$output" == *"Usage"* ]]
    [[ "$output" == *"Commands"* ]]
}

@test "norn version prints version" {
    autoreconf -fi
    ./configure --prefix="$WORK_DIR/install"
    make
    make install
    
    run "$WORK_DIR/install/bin/norn" version
    [ "$status" -eq 0 ]
    [[ "$output" == *"norn"* ]]
}

@test "pkg-config files are installed" {
    autoreconf -fi
    ./configure --prefix="$WORK_DIR/install"
    make
    make install
    
    [ -f "$WORK_DIR/install/lib/pkgconfig/norn.pc" ]

    # The .pc lives under the test prefix, so point pkg-config at it.
    PKG_CONFIG_PATH="$WORK_DIR/install/lib/pkgconfig" \
        run pkg-config --cflags norn
    [ "$status" -eq 0 ]
}

@test "static library is built" {
    autoreconf -fi
    ./configure --prefix="$WORK_DIR/install" --enable-static
    make
    
    # Static library should exist
    [ -f "$WORK_DIR/.libs/libnorn.a" ] || [ -f "$WORK_DIR/libnorn.a" ]
}

@test "shared library is built" {
    autoreconf -fi
    ./configure --prefix="$WORK_DIR/install" --enable-shared
    make
    
    # Shared library should exist
    ls .libs/libnorn.so* || ls libnorn.so* || ls .libs/libnorn.dylib*
}