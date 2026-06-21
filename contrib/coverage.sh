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
    
    LINE=$(lcov --rc branch_coverage=1 --summary coverage.info 2>&1 | grep "$src" | grep -oP 'Lines.*' || echo "Lines: 0%")
    BRANCH=$(lcov --rc branch_coverage=1 --summary coverage.info 2>&1 | grep "$src" | grep -oP 'Branches.*' || echo "Branches: 0%")
    
    LINE_PCT=$(echo "$LINE" | grep -oP '\d+' | head -1)
    BRANCH_PCT=$(echo "$BRANCH" | grep -oP '\d+' | head -1)
    
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