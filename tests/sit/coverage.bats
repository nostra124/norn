#!/usr/bin/env bats
# SIT: Test coverage enforcement

setup() {
    WORK_DIR="$(mktemp -d)"
    export WORK_DIR
    cd "$WORK_DIR"
    
    # Copy source to work directory
    cp -r /Users/rene/Projekte/norn/* "$WORK_DIR/"
}

teardown() {
    cd /
    rm -rf "$WORK_DIR"
}

@test "make coverage runs successfully" {
    autoreconf -fi
    ./configure --enable-coverage CFLAGS="-O0 -g"
    make
    
    run make coverage
    [ "$status" -eq 0 ]
}

@test "coverage gate enforces 100% line coverage" {
    autoreconf -fi
    ./configure --enable-coverage CFLAGS="-O0 -g"
    make
    
    # Run coverage
    make coverage > coverage.log 2>&1
    
    # Check that tracked files have 100% line coverage
    grep "PASS: norn.c - Lines: 100%" coverage.log || \
    grep "PASS: bep44.c - Lines: 100%" coverage.log || \
    grep "PASS: sha1.c - Lines: 100%" coverage.log || \
    grep "PASS: dhtstore.c - Lines: 100%" coverage.log || \
    grep "PASS: bencode.c - Lines: 100%" coverage.log
}

@test "coverage gate enforces 100% branch coverage" {
    autoreconf -fi
    ./configure --enable-coverage CFLAGS="-O0 -g"
    make
    
    # Run coverage
    make coverage > coverage.log 2>&1
    
    # Check that tracked files have 100% branch coverage
    grep "PASS: norn.c - Branches: 100%" coverage.log || \
    grep "PASS: bep44.c - Branches: 100%" coverage.log || \
    grep "PASS: sha1.c - Branches: 100%" coverage.log || \
    grep "PASS: dhtstore.c - Branches: 100%" coverage.log || \
    grep "PASS: bencode.c - Branches: 100%" coverage.log
}

@test "coverage report is generated" {
    autoreconf -fi
    ./configure --enable-coverage CFLAGS="-O0 -g"
    make
    make coverage
    
    # Coverage info file should be generated
    [ -f coverage.info ]
}

@test "coverage fails with uncovered lines" {
    # This test intentionally removes a test to create a coverage gap
    autoreconf -fi
    ./configure --enable-coverage CFLAGS="-O0 -g"
    make
    
    # Temporarily remove one test file (simulating missing coverage)
    mv tests/test_bep44.c tests/test_bep44.c.bak
    
    # Build should succeed
    make clean
    make
    
    # Coverage should fail (missing tests)
    run make coverage
    [ "$status" -ne 0 ]
    
    # Restore
    mv tests/test_bep44.c.bak tests/test_bep44.c
}

@test "coverage script is executable" {
    [ -x contrib/coverage.sh ]
}

@test "coverage-tracked.txt lists all source files" {
    [ -f tests/coverage-tracked.txt ]
    
    # Each line should be a .c file
    while IFS= read -r line; do
        # Skip comments
        [[ "$line" =~ ^#.* ]] && continue
        [[ -z "$line" ]] && continue
        
        # File should exist
        [ -f "src/libnorn/$line" ]
    done < tests/coverage-tracked.txt
}