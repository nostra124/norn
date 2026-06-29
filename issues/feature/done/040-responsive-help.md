---
id: FEAT-040
type: feature
priority: low
complexity: XS
estimate_tokens: 1k-2k
estimate_time: 2-5min
phase: planned
status: done
depends_on: ~
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# FEAT-040 — Terminal-width-aware `norn --help` output

## Description

**As a** user on a small terminal (e.g. iPhone SSH client)
**I want** `norn --help` to wrap at the actual terminal width
**So that** I can read the full help without horizontal scrolling.

## Changes

### 1. `term_width()` helper

Queries terminal width via `TIOCGWINSZ` ioctl (with `COLUMNS` env var
fallback, then hard default of 80). No termcap/ncurses dependency.

### 2. `wrap_print()` helper

Word-wraps a string to the given width, respecting existing newlines.
Used for both command descriptions and option descriptions.

### 3. Refactored `usage()`

The hard-coded `usage()` string was replaced with structured calls to
`fprintf(stdout, ...)` + `wrap_print()` for each section (usage line,
command list with descriptions, option list with descriptions).

## Files

- `src/norn.c` — `term_width()`, `wrap_print()`, refactored `usage()`
- `src/libnorn/norn.h` — no changes needed (internal helpers)

## Tests

- Manual: `norn --help` wraps at terminal width; `COLUMNS=40 norn --help`
  wraps at 40 columns; `COLUMNS=999 norn --help` uses single-line format.
