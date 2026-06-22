# norn Roadmap

## Current Status (2026-06-22)

### Coverage Summary

**20 modules tracked** for 100% line AND branch coverage

| Module | Lines | Branches | Status |
|--------|-------|----------|--------|
| norn.c | 26.1% | 60.0% | ❌ CRITICAL |
| norn_impl.c | 73.6% | 93.3% | ⚠️ Needs work |
| norn_transaction.c | 100% | 100% | ✅ PASS |
| kademlia.c | 80.7% | 100% | ⚠️ Needs work |
| bep44.c | 98.3% | 100% | ⚠️ Near done |
| sha1.c | 100% | 100% | ✅ PASS |
| dhtstore.c | 53.6% | 50.0% | ❌ CRITICAL |
| recstore.c | 91.3% | 100% | ⚠️ Near done |
| bencode.c | 82.6% | 100% | ⚠️ Needs work |
| crypto.c | 95.4% | 100% | ⚠️ Near done |
| net.c | 18.9% | 19.0% | ❌ CRITICAL |
| log.c | 100% | 100% | ✅ PASS |
| channel.c | 94.4% | 95.5% | ⚠️ Near done |
| replaycache.c | 100% | 100% | ✅ PASS |
| stream.c | 100% | 100% | ✅ PASS |
| streammux.c | 100% | 100% | ✅ PASS |
| transport.c | 100% | 100% | ✅ PASS |
| transport_udp.c | 100% | 100% | ✅ PASS |
| transport_tcp.c | 100% | 100% | ✅ PASS |
| idexch.c | 100% | 100% | ✅ PASS |
| attr.c | 100% | 100% | ✅ PASS |

**Summary:**
- ✅ **11/20 modules PASS** (55%)
- ⚠️ **6/20 modules near completion** (30%) - need <10% more coverage
- ❌ **3/20 modules CRITICAL** (15%) - need significant work

### Modules Extracted from bifrost

The following modules were extracted from the bifrost project and are now part of norn:

1. **stream** - Stream handling (100% coverage) ✅
2. **streammux** - Stream multiplexing (100% coverage) ✅
3. **transport** - Transport abstraction layer (100% coverage) ✅
4. **transport_udp** - UDP transport implementation (100% coverage) ✅
5. **transport_tcp** - TCP transport implementation (100% coverage) ✅
6. **channel** - Channel management (94.4% coverage) ⚠️
7. **replaycache** - Replay attack prevention cache (100% coverage) ✅
8. **idexch** - Identity exchange protocol (100% coverage) ✅
9. **attr** - Node attributes API (100% coverage) ✅

---

## Phase 1: Core Coverage (Current)

**Goal:** Reach 100% coverage on all 20 tracked modules

### Priority 1: Critical Modules (< 60% coverage)

These modules need significant test development:

| Module | Current | Target | Work Needed |
|--------|---------|--------|-------------|
| norn.c | 26.1% | 100% | Network mocking for packet processing |
| net.c | 18.9% | 100% | Socket abstraction layer for tests |
| dhtstore.c | 53.6% | 100% | Eviction, TTL, per-IP cap tests |

### Priority 2: Near-Done Modules (80-99% coverage)

These modules need small additions:

| Module | Current | Gap | Missing Tests |
|--------|---------|-----|---------------|
| channel.c | 94.4% | 5.6% | Edge cases |
| crypto.c | 95.4% | 4.6% | Error paths |
| bep44.c | 98.3% | 1.7% | NULL input handling |
| recstore.c | 91.3% | 8.7% | Boundary conditions |
| bencode.c | 82.6% | 17.4% | Error paths, invalid input |
| kademlia.c | 80.7% | 19.3% | Routing table edge cases |
| norn_impl.c | 73.6% | 26.4% | Async callback paths |

---

## Phase 2: Production Ready

**Goal:** Production-ready library with documentation and packaging

### 2.1 Documentation
- [ ] API reference documentation (FEAT-004)
- [ ] Architecture documentation (FEAT-003)
- [ ] Man page for CLI (FEAT-011)
- [ ] Contributing guide (FEAT-005)

### 2.2 Packaging
- [ ] Homebrew formula (Formula/norn.rb exists, needs testing)
- [ ] Linux package support (Alpine, Debian, Fedora, etc.)

### 2.3 CI/CD
- [ ] GitHub Actions pipeline (FEAT-008)
- [ ] Static analysis integration (FEAT-009)

---

## Phase 3: Future Enhancements

### 3.1 Code Refactoring (FEAT-006)
- Split norn.c (1700+ lines) into focused modules
- Target: <500 lines per file

### 3.2 Build System Cleanup (FEAT-007)
- Remove duplicate macros in configure.ac
- Consolidate Makefile.am patterns

---

## Test Infrastructure

### Unit Tests (Current Focus)
- Location: `tests/test_*.c`
- Framework: Custom assertion macros
- Run: `make check`
- Coverage: `make coverage`

### SIT Tests
- Location: `tests/sit/*.bats`
- Framework: bats (Bash Automated Testing System)
- Coverage: Build, install, CLI operations
- Status: ✅ Created (40 tests)

### PIT Tests
- Location: `tests/pit/*.bats`
- Coverage: Network operations
- Requires: Podman/Docker containers
- Status: ✅ Created (12 tests)

---

## Commit History

| Commit | Description |
|--------|-------------|
| b5e55ad | test: add unit tests for new modules (bencode, kademlia, log, transport_tcp) |
| 4577c33 | NORN-010: Add node attributes API |
| 7253145 | NORN-009: Move idexch (identity exchange) from bifrost |
| 265d5e6 | NORN-007: Move transport abstraction from bifrost |
| 40aeda5 | NORN-006: add streammux from bifrost |
| c56a8c7 | NORN-005: add stream from bifrost |
| 01ac69e | NORN-004: add channel and replaycache from bifrost |
| ac04b8a | feat: comprehensive test coverage improvements |