#!/bin/bash
# Coverage gate for norn
set -e
cd "$(dirname "$0")/.."

TRACKED="tests/coverage-tracked.txt"

echo "==> Building with coverage"
./configure --enable-coverage CFLAGS="-O0 -g"
make clean
make

# Drop any stale .gcda counters (e.g. from manually running the daemon) so they
# can't mismatch the freshly built .gcno notes and abort the lcov capture. Only
# the .gcda produced by `make check` below should feed the report.
find . -name '*.gcda' -delete

echo "==> Running tests"
make check

echo "==> Capturing coverage"
lcov --rc branch_coverage=1 --capture --directory . --output-file coverage.info 2>/dev/null || \
    lcov --rc lcov_branch_coverage=1 --capture --directory . --output-file coverage.info

echo "==> Checking coverage for tracked sources"
# Parse the raw .info records directly. lcov 2.0's `--list` table renders
# bogus percentages with some gcov builds (functions >100%, branches blanked),
# but the underlying SF/LF/LH/BRF/BRH records are accurate. The gate reads
# those: a source passes when every instrumented line and branch is hit
# (LH==LF and BRH==BRF), after LCOV_EXCL markers have removed untestable paths.
FAILED=0
while read -r src; do
    [ -z "$src" ] && continue
    [ "${src:0:1}" = "#" ] && continue

    RESULT=$(awk -v target="$src" '
        function endswith(s, suf) {
            return length(s) >= length(suf) &&
                   substr(s, length(s) - length(suf) + 1) == suf
        }
        /^SF:/ { cur = endswith(substr($0, 4), target)
                 if (cur) { lf=lh=brf=brh=0 } }
        cur && /^LF:/  { lf  = substr($0, 4) }
        cur && /^LH:/  { lh  = substr($0, 4) }
        cur && /^BRF:/ { brf = substr($0, 5) }
        cur && /^BRH:/ { brh = substr($0, 5) }
        cur && /^end_of_record/ { print lf, lh, brf, brh; cur=0 }
    ' coverage.info | head -1)

    if [ -z "$RESULT" ]; then
        echo "FAIL: $src - Not found in coverage report"
        FAILED=1
        continue
    fi

    set -- $RESULT
    LF=$1; LH=$2; BRF=$3; BRH=$4

    if [ "$LF" = "0" ]; then
        echo "FAIL: $src - No instrumented lines"
        FAILED=1
        continue
    fi

    if [ "$LH" != "$LF" ] || [ "$BRH" != "$BRF" ]; then
        echo "FAIL: $src - Lines: ${LH}/${LF}, Branches: ${BRH}/${BRF}"
        FAILED=1
    else
        echo "PASS: $src - Lines: ${LH}/${LF}, Branches: ${BRH}/${BRF}"
    fi
done < "$TRACKED"

if [ $FAILED -eq 1 ]; then
    echo "Coverage gate FAILED"
    exit 1
fi

echo "Coverage gate PASSED"