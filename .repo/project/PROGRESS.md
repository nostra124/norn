# Progress

## Goal
- Achieve production-ready norn library with multi-consumer crypto/pluggable infrastructure (v0.7.0 complete, v0.8.0 in progress)

## Constraints & Preferences
- 100% line AND branch coverage for unit-testable modules (where achievable without error injection)
- Network I/O modules (norn.c, norn_impl.c, net.c) excluded from unit tests — require SIT/PIT
- Tests must follow dvalin methodology (FEAT-NNN naming)
- Milestones tracked in `.repo/project/issues/MILESTONE-X.Y.Z.md`
- Build with `-Werror` (all warnings are errors)
- VERSION file is single source of truth for version (currently 0.7.0)

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
- **v0.8.0 Dial & Session Orchestration** (Phase 1 COMPLETE):
  - FEAT-016 Phase 1: **Async & Mobile-Ready Session API**
    - Fully async, non-blocking API (no blocking I/O)
    - Event loop integration via `norn_tick()` and `norn_get_fd()`
    - Mobile-ready: iOS (CFRunLoop), Android (epoll)
    - Session tracking in `norn_client_t`
    - Callback-based lifecycle
    - All tests passing (30/30)

### In Progress
- **FEAT-016** Phase 2: NAT traversal integration (FEAT-017)
- **FEAT-016** Phase 3: Platform-specific event loop adapters (FEAT-019)

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

## Next Steps
1. FEAT-017: NAT traversal (hole-punch, relay)
2. FEAT-018: Stream multiplexing implementation
3. FEAT-019: Platform event loop adapters

## Critical Context
- **Tests**: 30/30 passing
- **Build**: Clean build with `-Werror`, all tests pass
- **Version**: 0.7.0 (from VERSION file)
- **Architecture**: Fully async, mobile-ready
- **Session API**: Complete async flow (dial, listen, close)
- **Event loop**: `norn_tick()` processes all sessions + DHT

## Relevant Files
- `VERSION` — Single source of truth for version (0.7.0)
- `src/libnorn/norn_session.h` — Async session API (FEAT-016)
- `src/libnorn/norn_session.c` — Async session implementation
- `src/libnorn/norn_internal.h` — Internal struct definitions
- `src/libnorn/norn_impl.c` — Client with session tracking
- `.repo/project/issues/FEAT-016-ASYNC.md` — Architecture design
- `.repo/project/issues/MILESTONE-0.8.0.md` — v0.8.0 milestone definition