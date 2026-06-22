# v0.2.0 — Core Stability

Foundation work ensuring robust, well-tested core functionality with 100% coverage.

## Status (2026-06-22)

**Coverage: 11/20 modules pass (55%)**

### Critical Modules (< 60% coverage)
- ❌ norn.c: 26.1% lines, 60.0% branches
- ❌ net.c: 18.9% lines, 19.0% branches  
- ❌ dhtstore.c: 53.6% lines, 50.0% branches

### Near-Done Modules (80-99% coverage)
- ⚠️ channel.c: 94.4% lines, 95.5% branches
- ⚠️ crypto.c: 95.4% lines, 100% branches
- ⚠️ bep44.c: 98.3% lines, 100% branches
- ⚠️ recstore.c: 91.3% lines, 100% branches
- ⚠️ bencode.c: 82.6% lines, 100% branches
- ⚠️ kademlia.c: 80.7% lines, 100% branches
- ⚠️ norn_impl.c: 73.6% lines, 93.3% branches

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