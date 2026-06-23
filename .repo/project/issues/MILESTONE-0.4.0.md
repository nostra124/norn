# v0.4.0 — Code Quality

Refactoring for maintainability.

## Status (2026-06-23)

**DONE** — Build system cleanup complete

## Tickets

| ID | Title | Status |
|----|-------|--------|
| FEAT-006 | Split norn.c into Focused Modules | done (decided against - norn.c is 963 lines, acceptable) |
| FEAT-007 | Build System Cleanup | done |

## Changes

### FEAT-007: Build System Cleanup

**Completed:**
1. ✅ Removed duplicate macros from `configure.ac` (AC_PROG_CC, AM_PROG_AR, LT_INIT)
2. ✅ Added `-Werror` to all compilation flags
3. ✅ Fixed unused parameter/function warnings
4. ✅ All 26 tests pass with `-Werror`
5. ✅ `make distcheck` passes (after adding headers to include_HEADERS)
6. ✅ All headers installed properly

**Not Done:**
- FEAT-006: Decided against splitting `norn.c` — at 963 lines it's within acceptable limits (<1000 LOC), and the public API (`norn.h`) already provides clean separation from protocol implementation

### Additional Cleanup

- Added all public headers to `include_HEADERS` in `Makefile.am`
- Added headers to `EXTRA_DIST` for distribution tarball