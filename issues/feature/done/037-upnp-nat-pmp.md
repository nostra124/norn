---
id: FEAT-037
type: feature
priority: medium
complexity: M
estimate_tokens: 8k-15k
estimate_time: 30-60min
phase: planned
status: done
depends_on: ~
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# FEAT-037 — NAT-PMP / UPnP automatic port mapping

## Description

**As a** node operator
**I want** libnorn to automatically map a UDP port on my router
**So that** my node is publicly reachable from the Internet without manual
router configuration.

## Changes

### 1. Full `norn_upnp.c` implementation

The previously-stubbed `norn_upnp.c` was filled in with real SSDP discovery,
device description fetching, and SOAP `AddPortMapping`. The function
`norn_auto_port_mapping()` implements a two-tier fallback:

1. **NAT-PMP first** — calls `natpmp_map_udp()` in `net.c` (already
   implemented, detects gateway via default route, sends NAT-PMP mapping
   request).
2. **UPnP fallback** — if NAT-PMP fails, sends SSDP M-SEARCH to
   `239.255.255.250:1900`, parses the device description URL from the
   response, fetches `rootDesc.xml`, extracts the `WANIPConnection` control
   URL, and issues a SOAP `AddPortMapping` request.

Both paths return a `norn_upnp_result_t` with `success`, `external_ip`,
and `external_port`.

### 2. Integration in `norn_listen_async()`

In `src/libnorn/norn_session.c`, after the UDP socket is bound and DHT
bootstrap is initiated, `norn_auto_port_mapping()` is called. On success,
the mapped external endpoint is fed back into the DHT layer via
`net_set_mapped_endpoint()` so the node announces the correct public
address.

### 3. Build system

`norn_upnp.c` added to `Makefile.am` `libnorn_la_SOURCES`.

### 4. Stub header

`norn_upnp.h` existed as a stub; no changes to the header were needed
since the API signature was already designed.

## Files

- `src/libnorn/norn_upnp.c` — full SSDP/SOAP/NAT-PMP implementation
- `src/libnorn/norn_upnp.h` — unchanged (API was pre-designed)
- `src/libnorn/norn_session.c` — NAT traversal in `norn_listen_async()`
- `src/libnorn/net.c` — `natpmp_map_udp()` and `natpmp_gateway()` (pre-existing)
- `Makefile.am` — added `norn_upnp.c` to sources

## Tests

- `tests/test_norn_upnp.c` — `test_auto_port_mapping_natpmp`,
  `test_auto_port_mapping_upnp`, `test_auto_port_mapping_both_fail`
  (requires network; skipped if offline)
