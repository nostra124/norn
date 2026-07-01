# FEAT-013: peer-fetch unit tests, SIT tests, and coverage enforcement

**Status**: done  
**Component**: `tests/test_nornd_peer_fetch.c`, `tests/sit/cli.bats`, `tests/coverage-tracked.txt`

## Summary

Add comprehensive testing for the peer-fetch IPC path implemented in FEAT-012:
1. Unit tests for all validation paths in `nornd_peer_fetch()` that return before network I/O.
2. SIT tests for all `norn peer` subcommands (connect/get/cat/list/public).
3. Add `peer_fetch.c` to coverage tracking with LCOV exclusion markers on the network I/O section.

## Unit Tests (`tests/test_nornd_peer_fetch.c`)

10 test functions covering these validation paths:
- NULL arguments (client, spec, verb, out, outlen)
- Spec too long (≥160 chars)
- `@host` missing port after `@`
- Bad 40-hex node-id (non-hex chars)
- Bad 64-hex pubkey (non-hex chars)
- Wrong spec length (not 40 or 64)
- Unknown verb
- Bad host (inet_pton failure on `pubkey@invalid:port`)
- Served-KV request encoding failure (GET with empty arg)
- NULL err pointer safety

## SIT Tests (`tests/sit/cli.bats`)

12 new tests added:
- `peer --help` shows subcommands
- `peer` with no subcommand shows error
- `peer connect` without spec shows usage
- `peer connect` with invalid spec fails (IPC error)
- `peer get` without args shows usage
- `peer get` with invalid spec fails (IPC error)
- `peer cat` without args shows usage
- `peer cat` with invalid spec fails (IPC error)
- `peer list` returns valid TSV
- `peer public` without node-id shows usage
- `peer public` with invalid node-id shows error
- `peer` with unknown subcommand shows error

## Coverage Tracking

- `peer_fetch.c` added to `tests/coverage-tracked.txt`
- Network I/O section (lines 183–295) wrapped in `LCOV_EXCL_START`/`LCOV_EXCL_STOP`
- Validation paths (lines 99–178) remain under 100% coverage enforcement

## Pre-existing Bug Fixes

- `tests/sit/coverage.bats`: fixed file-exists path check from `src/libnorn/$line` to `$line` (paths are project-root relative)
- `tests/sit/nornd.bats`: fixed cluster-members tab regex (literal `\t` → `$'\t'`)
