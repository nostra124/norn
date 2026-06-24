# Progress

## Goal
- Achieve production-ready norn library with multi-consumer crypto/pluggable infrastructure (v0.8.0 complete, v0.9.0 in progress)

## Constraints & Preferences
- 100% line AND branch coverage for unit-testable modules (where achievable without error injection)
- Network I/O modules (norn.c, norn_impl.c, net.c) excluded from unit tests — require SIT/PIT
- Tests must follow dvalin methodology (FEAT-NNN naming)
- Milestones tracked in `.repo/project/issues/MILESTONE-X.Y.Z.md`
- Build with `-Werror` (all warnings are errors)
- VERSION file is single source of truth for version (currently 0.9.0-dev)

## Progress
### Done
- **v0.2.0 Core Stability**: 84% coverage, async API, logging module
- **v0.3.0 Documentation**: architecture.md, BEP-REFERENCES.md, PORTING.md, CONTRIBUTING.md, Doxygen headers
- **v0.4.0 Code Quality**: Build cleanup, `-Werror`, all headers installed
- **v0.5.0 Infrastructure**: CI/CD (GitHub Actions), codecov.yml, static analysis
- **v0.6.0 User Experience**: CLI (keygen, get, set, daemon, version), man page, VERSION file
- **v0.7.0 Multi-Consumer Foundation** (COMPLETE):
  - FEAT-013: Pluggable crypto suite vtable — `norn_suite.h`, `norn_suite_sodium.c`
  - FEAT-014: Parameterised Kademlia ID width — `norn_kad.h`, `norn_kad.c`
  - FEAT-015: De-application-ise idexch — `norn_idexch.h/c` (generic, opaque payload)
- **v0.8.0 Dial & Session Orchestration** (COMPLETE):
  - FEAT-016 Phase 1: **Async & Mobile-Ready Session API**
    - Fully async, non-blocking API (no blocking I/O)
    - Event loop integration via `norn_tick()` and `norn_get_fd()`
    - Mobile-ready: iOS (CFRunLoop), Android (epoll)
    - Session tracking in `norn_client_t`
    - Callback-based lifecycle
  - FEAT-017 Phase 1-2: **NAT Traversal**
    - Endpoint discovery with async DHT queries
    - Endpoint cache with TTL (5min default)
    - Direct connection integration
    - Connection ladder: resolve → direct → hole-punch → relay
  - All tests passing (30/30)

### In Progress
- **FEAT-017** Phase 3: Hole punching (3 days)
- **FEAT-017** Phase 4: Relay fallback (4 days)
- **FEAT-017** Phase 5: Integration (2 days)

### Blocked
- None

## Key Decisions
- Crypto suite vtable enables multiple backends (Ed25519 for bifrost, secp256k1 for wyrd)
- Kademlia routing table parameterized on `suite->nodeid_len`
- Public mainline client stays fixed 20-byte Ed25519 (external spec), native overlay is parameterized
- VERSION file is authoritative for version string
- **All session APIs are async** - no blocking I/O anywhere
- **Event loop agnostic** - integrates with libuv, epoll, kqueue, CFRunLoop
- **Battery efficient** - uses notification-based I/O, not polling
- **Connection ladder** - resolve → direct → hole-punch → relay

## Next Steps
1. FEAT-017 Phase 3: Hole punching with rendezvous
2. FEAT-017 Phase 4: Relay fallback
3. FEAT-018: Stream multiplexing implementation
4. FEAT-019: Platform event loop adapters

## Critical Context
- **Tests**: 30/30 passing
- **Build**: Clean build with `-Werror`, all tests pass
- **Version**: 0.9.0-dev (from VERSION file)
- **Architecture**: Fully async, mobile-ready, NAT traversal foundation
- **Session API**: Complete async flow (dial, listen, close)
- **Event loop**: `norn_tick()` processes all sessions + DHT
- **NAT Traversal**: Endpoint cache, direct connection, hole punch/relay pending

## Relevant Files
- `VERSION` — Single source of truth for version (0.9.0-dev)
- `src/libnorn/norn_session.h` — Async session API (FEAT-016)
- `src/libnorn/norn_session.c` — Async session implementation
- `src/libnorn/norn_endpoint_cache.h/c` — Endpoint cache (FEAT-017)
- `src/libnorn/norn_internal.h` — Internal struct definitions
- `src/libnorn/norn_impl.c` — Client with session tracking
- `.repo/project/issues/FEAT-016-ASYNC.md` — Architecture design
- `.repo/project/issues/FEAT-017-NAT.md` — NAT traversal design
- `.repo/project/issues/MILESTONE-0.9.0.md` — v0.9.0 milestone definition