# Progress

## Goal
- Achieve production-ready norn library with multi-consumer crypto/pluggable infrastructure (v0.8.0 mostly done, v0.9.0 in progress)

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
- **v0.8.0 Dial & Session Orchestration** (MOSTLY DONE):
  - FEAT-016: Async & Mobile-Ready Session API
    - Fully async, non-blocking API (no blocking I/O)
    - Event loop integration via `norn_tick()` and `norn_get_fd()`
    - Mobile-ready: iOS (CFRunLoop), Android (epoll)
    - Session tracking in `norn_client_t`
    - Callback-based lifecycle
  - FEAT-017: NAT Traversal (PARTIAL)
    - Phase 1: Endpoint discovery with async DHT queries ✅
    - Phase 2: Direct connection integration ✅
    - Phase 3: Hole punch wire protocol ✅
    - Phase 3: Hole punch integration ❌ (FEAT-023 needed)
    - Phase 4: Relay wire protocol ✅
    - Phase 4: Relay path integration ❌ (FEAT-022 needed)
    - Phase 5: Connection ladder ✅ (but needs FEAT-023, FEAT-022)
    - UPnP/NAT-PMP ❌ (FEAT-021 needed)
  - All tests passing (32/32)

### In Progress
- **FEAT-021**: UPnP/NAT-PMP automatic port forwarding
- **FEAT-022**: Multi-hop relay path integration
- **FEAT-023**: Hole punch connection integration
- **FEAT-018**: Stream multiplexing (`norn_stream_open_async`)
- **FEAT-019**: Language bindings (Rust, Python)

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
- **Static relay** - not anonymous, performance-first, trusted friend nodes
- **Binary protocol** - separate from bencode DHT (message types 0x10-0x2F)
- **Rendezvous = Introducer** - coordinates hole punching between NAT'd peers

## Next Steps
1. FEAT-021: UPnP/NAT-PMP implementation (automatic port forwarding)
2. FEAT-022: Multi-hop relay path integration (final fallback)
3. FEAT-023: Hole punch connection integration (wire up rendezvous)
4. FEAT-018: Stream multiplexing implementation
5. FEAT-019: Platform event loop adapters

## Critical Context
- **Tests**: 32/32 passing
- **Build**: Clean build with `-Werror`, all tests pass
- **Version**: 0.9.0-dev (from VERSION file)
- **Architecture**: Fully async, mobile-ready, NAT traversal foundation complete
- **Session API**: Complete async flow (dial, listen, close)
- **Event loop**: `norn_tick()` processes all sessions + DHT + NAT messages
- **NAT Traversal**: Wire protocols complete, integration pending

## Relevant Files
- `VERSION` — Single source of truth for version (0.9.0-dev)
- `src/libnorn/norn_session.h` — Async session API (FEAT-016)
- `src/libnorn/norn_session.c` — Async session implementation
- `src/libnorn/norn_endpoint_cache.h/c` — Endpoint cache (FEAT-017)
- `src/libnorn/norn_nat.h/c` — NAT wire protocol (FEAT-017)
- `src/libnorn/norn_rendezvous.h/c` — Hole punch coordination (FEAT-017)
- `src/libnorn/norn_relay.h/c` — Static relay (FEAT-017)
- `src/libnorn/norn_upnp.h/c` — UPnP/NAT-PMP (FEAT-021)
- `src/libnorn/norn_internal.h` — Internal struct definitions
- `src/libnorn/norn_impl.c` — Client with session tracking
- `.repo/project/issues/FEAT-021-UPNP.md` — UPnP implementation tracking
- `.repo/project/issues/FEAT-022-MULTIPATH-RELAY.md` — Relay integration tracking
- `.repo/project/issues/FEAT-023-HOLEPUNCH-INTEGRATION.md` — Hole punch integration tracking
- `.repo/project/issues/FEAT-016-ASYNC.md` — Architecture design
- `.repo/project/issues/FEAT-017-NAT.md` — NAT traversal design
- `.repo/project/ROADMAP.md` — Milestone overview
- `docs/NAT-TRAVERSAL.md` — NAT traversal design document