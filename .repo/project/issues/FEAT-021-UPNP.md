# FEAT-021 UPnP/NAT-PMP Automatic Port Forwarding

## Status: DEFERRED → post-1.0 (decided 2026-06-27)

**Decision:** formally deferred past the 1.0 release. An *optional* NAT-traversal
enhancement — core traversal (rendezvous hole-punch, FEAT-023) already covers
1.0. A real implementation drives a live UPnP/NAT-PMP gateway (SSDP M-SEARCH,
SOAP AddPortMapping, NAT-PMP to :5351), which the unit/CI methodology cannot
verify (no router in CI) — it is PIT-only. Parked until after GA to keep the
march to the stabilization gates honest rather than ship untestable router code.
The `norn_upnp.h` API is reserved; `norn_upnp.c` stubs remain returning -1.

## (original) Status: INCOMPLETE (API defined, implementation missing)

## Description

Implement UPnP and NAT-PMP for automatic port mapping. This is the **first choice** for NAT traversal before attempting hole punching or relays.

## API Defined

```c
// src/libnorn/norn_upnp.h
int norn_upnp_discover(uint32_t timeout_ms, char *device_url);
int norn_upnp_add_mapping(const char *device_url, uint16_t internal_port, ...);
int norn_upnp_remove_mapping(const char *device_url, uint16_t external_port, ...);
int norn_natpmp_add_mapping(uint32_t gateway_ip, uint16_t internal_port, ...);
int norn_auto_port_mapping(uint16_t internal_port, const char *protocol, ...);
```

## Current Implementation

All functions return `-1` (not implemented). Need:

### UPnP Implementation
1. SSDP discovery (M-SEARCH to 239.255.255.250:1900)
2. SOAP requests for AddPortMapping/DeletePortMapping
3. GetExternalIPAddress
4. Parse device responses

### NAT-PMP Implementation
1. UDP to gateway:5351
2. Map request (opcode 1=UDP, 2=TCP)
3. Parse external port from response

## Integration Points

- Call from `norn_dial_async()` before hole punching
- Update `norn_endpoint_t` with external IP after successful mapping
- Store mapping for cleanup on session close

## Priority: Medium

This improves success rate for NAT traversal significantly on home routers.

## Estimated Effort: 2-3 days

## Related: FEAT-017 (NAT Traversal)