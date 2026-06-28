---
id: FEAT-036
type: feature
priority: medium
complexity: M
estimate_tokens: 8k-15k
estimate_time: 20-40min
phase: planned
status: done
depends_on: [FEAT-034]
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# FEAT-036 — Bonjour/mDNS service announcement and discovery for norn nodes

## Description

**As a** norn node operator
**I want** nodes on the same LAN to find each other automatically via
Bonjour/mDNS
**So that** they can bootstrap from local peers without manual configuration.

## Implementation

### New module: `norn_bonjour.c` / `norn_bonjour.h`

A new module in `src/libnorn/` that uses Avahi (the Linux mDNS/DNS-SD
implementation) to:

1. **Announce** this norn node as a `_norn._udp` service on the LAN.
2. **Browse** for other `_norn._udp` services and resolve their IP:port.
3. Add discovered nodes as **bootstrap seeds** via `mainline_add_bootstrap()`.

### Avahi integration

- Uses `AvahiThreadedPoll` (background thread) so mDNS event processing
  doesn't interfere with the main event loop.
- Service type: `_norn._udp`, instance name: `norn node`.
- TXT record includes `proto=norn-dht`.
- When `avahi-daemon` is not running, the module silently returns NULL.

### Optional dependency

Avahi is an **optional** dependency. The configure script checks for
`avahi-client >= 0.6` via `pkg-config`. When unavailable, the Bonjour
module compiles as stubs that return NULL, and `norn_listen_async()`
proceeds without mDNS.

### Integration points

- `norn_listen_async()` in `norn_session.c` calls `norn_bonjour_new()`
  after binding the UDP socket.
- `norn_free()` in `norn_impl.c` calls `norn_bonjour_free()`.
- The `norn_bonjour_t *` pointer is stored in `norn_client_t` (in
  `norn_internal.h`).

## Files

- `src/libnorn/norn_bonjour.c` — Avahi-based implementation
- `src/libnorn/norn_bonjour.h` — public interface
- `src/libnorn/norn_internal.h` — added `norn_bonjour_t *bonjour` field
- `src/libnorn/norn_session.c` — create Bonjour announcer on listen
- `src/libnorn/norn_impl.c` — free Bonjour announcer in `norn_free()`
- `Makefile.am` — added `norn_bonjour.c`, `$(AVAHI_CFLAGS)`, `$(AVAHI_LIBS)`
- `configure.ac` — optional `PKG_CHECK_MODULES([AVAHI], [avahi-client >= 0.6])`

## Build system

```
./autogen.sh && ./configure   # detects avahi-client
make                          # compiles with Bonjour support
make check                    # 51 tests pass
```

## Tests

- No dedicated unit test for the Avahi interaction (requires running
  `avahi-daemon`). The module is exercised indirectly via
  `test_listen_async_bootstrap` which creates a client and calls
  `norn_listen_async()` — the Bonjour init is best-effort and silently
  skipped.
