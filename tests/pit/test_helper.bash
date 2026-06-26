#!/usr/bin/env bash
# Test helper for SIT/PIT tests

# Set up environment
export CC="${CC:-gcc}"
# libnorn needs POSIX symbols under glibc, so build the helper probes with gnu99.
export CFLAGS="${CFLAGS:--Wall -Wextra -Werror -std=gnu99}"

# Resolve the project source tree. Override with NORN_SRC; otherwise derive it
# from this helper's location (tests/{sit,pit}/ -> project root). This replaces
# the previously hard-coded developer path so the suites run anywhere.
norn_src() {
    if [ -n "$NORN_SRC" ]; then
        printf '%s\n' "$NORN_SRC"
        return
    fi
    ( cd "$BATS_TEST_DIRNAME/../.." && pwd )
}

# Copy a clean source tree into $WORK_DIR for an isolated build+install. Uses
# `git archive` (committed tree, no build artifacts) when available, else a
# plain copy followed by a best-effort distclean.
copy_src() {
    local src
    src="$(norn_src)"
    if [ -d "$src/.git" ] && command -v git >/dev/null 2>&1; then
        ( cd "$src" && git archive --format=tar HEAD ) | tar -x -C "$WORK_DIR"
    else
        cp -r "$src"/. "$WORK_DIR/"
        ( cd "$WORK_DIR" && make distclean >/dev/null 2>&1 || true )
        rm -rf "$WORK_DIR/.git"
    fi
}
