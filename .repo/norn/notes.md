# Session Notes — norn

## 2026-06-21: Initial Audit

### Project Structure
- C library for Mainline DHT (libnorn)
- CLI tool (norn)
- Autotools build system
- libsodium dependency

### Critical Finding: Async API Not Implemented

**Status**: BLOCKER for production use

The async API declared in `norn.h` is not actually implemented:
- `norn_get_mutable` — stub, does nothing
- `norn_get_immutable` — stub, does nothing
- `norn_discover` — stub, does nothing
- `norn_tick` — stub, returns 0
- `norn_get_fd` — incomplete (returns -1 if no net)

Core functions in `mainline.c` are **blocking synchronous**:
- `mainline_lookup` — creates own socket, blocks on `select()`
- `mainline_resolve_node` — creates own socket, blocks on `select()`
- `mainline_lookup_mutable` — creates own socket, blocks on `select()`

This makes the library unusable for:
- Event-driven applications
- Applications with their own event loops
- Multi-threaded applications
- Any production use

**Fix**: FEAT-012 created to implement proper async API.

### Test Coverage

Created comprehensive test suite:
- `test_bencode.c` — 18 tests
- `test_crypto.c` — 21 tests
- `test_log.c` — 13 tests
- `test_dhtstore.c` — 16 tests (extended)
- `test_norn.c` — 22 tests (extended)
- `test_kademlia.c` — 10 tests
- `test_recstore.c` — 7 tests
- SIT tests: `tests/sit/*.bats` — 40 tests
- PIT tests: `tests/pit/*.bats` — 12 tests

### Integration Tests

Created SIT/PIT structure following dvalin methodology:
- SIT: Build, install, coverage, CLI (40 tests)
- PIT: Real DHT network operations (12 tests)

### Issues Found During Testing

1. `norn_get_id` — assertion checked for non-zero ID, but ID can be zero
2. `dhtstore_budget_exceeded` — assertion too strict
3. `bencode_encode_string` — wrong length calculation
4. `test_decode_malformed` — `le` and `de` are valid empty list/dict
5. `test_log_null_format` — passing NULL to printf is UB

All fixed.

### Milestone Structure

Created milestone files following dvalin methodology:
- `.repo/project/issues/MILESTONE-0.2.0.md` — Core Stability
- `.repo/project/issues/MILESTONE-0.3.0.md` — Documentation
- `.repo/project/issues/MILESTONE-0.4.0.md` — Code Quality
- `.repo/project/issues/MILESTONE-0.5.0.md` — Infrastructure
- `.repo/project/issues/MILESTONE-0.6.0.md` — User Experience

### Feature Tickets

Created following `FEAT-NNN` naming convention (no project prefix):
- FEAT-001: Unit Test Coverage to 100%
- FEAT-002: Logging Module
- FEAT-003: Architecture Documentation
- FEAT-004: API Reference Documentation
- FEAT-005: CONTRIBUTING Guide
- FEAT-006: Split norn.c into Modules
- FEAT-007: Build System Cleanup
- FEAT-008: CI/CD Pipeline
- FEAT-009: Static Analysis
- FEAT-010: CLI Implementation
- FEAT-011: Man Page
- FEAT-012: Async API (Critical)

### Build System Issues

- `configure.ac` has duplicate macros — fixed in FEAT-007
- `contrib/coverage.sh` not executable — fixed (chmod +x)
- Missing test targets in `Makefile.am` — fixed

### Documentation

Created:
- `docs/test-plan.md` — Comprehensive test plan
- `docs/milestones.md` — Milestone overview
- `.repo/project/profile.md` — Project profile

### Code Quality Observations

**Good**:
- Consistent C99 style
- NULL-safe public APIs
- Security-conscious (CSPRNG, token secrets)
- No heap allocations in hot paths

**Needs Work**:
- `norn.c` is 1100+ lines — split into modules (FEAT-006)
- Async API not implemented — FEAT-012
- CLI is stub — FEAT-010

### Next Steps

1. **Critical**: Implement FEAT-012 (async API)
2. Complete FEAT-001 (100% test coverage)
3. Run coverage gate
4. Add Homebrew formula (mentioned by user)