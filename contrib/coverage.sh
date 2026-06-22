#!/bin/bash
# Coverage gate for norn
set -e
cd "$(dirname "$0")/.."

TRACKED="tests/coverage-tracked.txt"

echo "==> Building with coverage"
./configure --enable-coverage CFLAGS="-O0 -g"
make clean
make

echo "==> Running tests"
make check

echo "==> Capturing coverage"
lcov --rc branch_coverage=1 --capture --directory . --output-file coverage.info 2>/dev/null || \
    lcov --rc lcov_branch_coverage=1 --capture --directory . --output-file coverage.info

echo "==> Checking coverage for tracked sources"
FAILED=0
while read -r src; do
    [ -z "$src" ] && continue
    [ "${src:0:1}" = "#" ] && continue
    
    LINE=$(lcov --list coverage.info 2>/dev/null | grep "$src" | head -1)
    if [ -z "$LINE" ]; then
        echo "FAIL: $src - Not found in coverage report"
        FAILED=1
        continue
    fi
    
    LINE_PCT=$(echo "$LINE" | awk -F'|' '{split($2,a,"%"); split(a[1],b," "); for(i in b) if(b[i] ~ /[0-9]/) {print b[i]; break}}')
    BRANCH_PCT=$(echo "$LINE" | awk -F'|' '{split($3,a,"%"); split(a[1],b," "); for(i in b) if(b[i] ~ /[0-9]/) {print b[i]; break}}')
    
    if [ -z "$LINE_PCT" ]; then
        LINE_PCT=0
    fi
    if [ -z "$BRANCH_PCT" ]; then
        BRANCH_PCT=0
    fi
    
    if [ "$LINE_PCT" != "100" ] || [ "$BRANCH_PCT" != "100" ]; then
        echo "FAIL: $src - Lines: ${LINE_PCT}%, Branches: ${BRANCH_PCT}%"
        FAILED=1
    else
        echo "PASS: $src - Lines: ${LINE_PCT}%, Branches: ${BRANCH_PCT}%"
    fi
done < "$TRACKED"

if [ $FAILED -eq 1 ]; then
    echo "Coverage gate FAILED"
    exit 1
fi

echo "Coverage gate PASSED"