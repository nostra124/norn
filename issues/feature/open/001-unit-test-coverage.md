---
id: FEAT-001
type: feature
priority: high
complexity: L
estimate_tokens: 80k-150k
estimate_time: 90-180min
phase: open
status: open
depends_on: []
milestone: MILESTONE-0.2.0
spawned_from: ~
---
# Unit Test Coverage to 100%

Achieve 100% line and branch coverage across all tracked source files.

## Description

**As a** norn library maintainer
**I want** comprehensive test coverage with enforced 100% line AND branch coverage
**So that** the library is robust, maintainable, and changes are validated automatically

The current test suite covers only happy paths in a few modules. Missing tests for error cases, NULL inputs, buffer boundaries, and several modules entirely (crypto, net, kademlia, recstore, log, norn_impl).

## Implementation

### Missing Test Files
- `tests/test_kademlia.c` — Kademlia protocol
- `tests/test_net.c` — Network layer
- `tests/test_crypto.c` — Crypto utilities
- `tests/test_recstore.c` — Record store
- `tests/test_log.c` — Logging
- `tests/test_norn_impl.c` — API wrapper

### Missing Coverage in Existing Tests
- `test_bep44.c`: Missing error paths (NULL inputs, buffer overflow)
- `test_dhtstore.c`: Missing eviction, TTL expiry, per-IP cap tests
- `test_norn.c`: Missing norn_bootstrap, norn_tick, norn_get_fd tests

### Test Categories

1. **NULL Input Tests** — Every public function must handle NULL inputs gracefully
2. **Buffer Overflow Tests** — Undersized buffers must be rejected, not truncated
3. **Boundary Tests** — Maximum sizes, TTL expiry, caps
4. **Allocation Failure Tests** — Use `LCOV_EXCL_LINE` for genuinely untestable paths

## Acceptance Criteria

1. ✅ All source files in `tests/coverage-tracked.txt` have 100% line coverage
2. ✅ All source files in `tests/coverage-tracked.txt` have 100% branch coverage
3. ✅ `make coverage` passes with zero failures
4. ✅ All tests pass on Linux, macOS, FreeBSD
5. ✅ New test files created for: kademlia, net, crypto, recstore, log
6. ✅ NULL input tests for all public APIs
7. ✅ Buffer boundary tests for encode/decode functions
8. ✅ LCOV_EXCL_LINE used only for genuinely untestable code

## Test Levels

This ticket covers **unit tests only**. Integration tests are tracked separately:

- **SIT (System Integration Tests)**: `tests/sit/*.bats` — Build, install, CLI (40 tests)
- **PIT (Performance Integration Tests)**: `tests/pit/*.bats` — Network operations (12 tests)

See `docs/test-plan.md` for full test matrix.