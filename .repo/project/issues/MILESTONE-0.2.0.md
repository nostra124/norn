# v0.2.0 — Core Stability

Foundation work ensuring robust, well-tested core functionality with 100% coverage.

## Status (2026-06-22)

**Coverage: 12/21 modules pass (57%)**

### Critical Modules (< 60% coverage)
- ❌ norn.c: 26.1% lines, 60.0% branches — **CRITICAL**: Needs network mocking for packet processing
- ⚠️ norn_impl.c: 73.6% lines, 93.3% branches — Needs async callback path tests

### Near-Done Modules (80-99% coverage)
- ⚠️ bep44.c: 99.1% lines, 100% branches — **1 line missing**
- ⚠️ crypto.c: 95.4% lines, 100% branches — ~10 lines missing
- ⚠️ channel.c: 94.4% lines, 95.5% branches — ~12 lines missing
- ⚠️ recstore.c: 91.3% lines, 100% branches — ~9 lines missing
- ⚠️ dhtstore.c: 85.7% lines, 87.5% branches — Budget/eviction tests added
- ⚠️ bencode.c: 82.6% lines, 100% branches — Error path tests exist
- ⚠️ net.c: 81.6% lines, 100% branches — Network layer tests added
- ⚠️ kademlia.c: 80.7% lines, 100% branches — Routing tests exist

### Passing Modules (100% coverage)
- ✅ norn_transaction.c
- ✅ sha1.c
- ✅ log.c
- ✅ replaycache.c
- ✅ stream.c
- ✅ streammux.c
- ✅ transport.c
- ✅ transport_udp.c
- ✅ transport_tcp.c
- ✅ idexch.c
- ✅ attr.c

## Tickets

| ID | Title | Status |
|----|-------|--------|
| FEAT-001 | Unit Test Coverage to 100% | in_progress |
| FEAT-002 | Logging Module with Full Coverage | done |
| FEAT-012 | Implement Async API (Critical) | done |