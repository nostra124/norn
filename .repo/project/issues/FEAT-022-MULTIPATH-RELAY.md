# FEAT-022 Multi-Hop Relay Paths

## Status: DEFERRED → post-1.0 (decided 2026-06-27)

**Decision:** formally deferred past the 1.0 release. The relay **wire protocol +
session management are complete** (`norn_relay.c`); what remains — DHT path
discovery, the chained RelayCreate/Forward/Accept connection, relay forwarding
(`handle_forward` TODO), and dial-state-machine integration — is the *final
fallback* in the connection ladder and needs real multi-hop relay topology to
verify (PIT-only). Core NAT traversal (hole-punch) already covers 1.0, so this
optional fallback is parked until after GA rather than shipped half-implemented.

**Knock-on:** FEAT-020 (private overlay) acceptance #2 — a NAT'd member reachable
via rendezvous/relay inside the overlay — depends on this and therefore also
defers post-1.0. v0.10.0 ships acceptance #1 for 1.0.

## (original) Status: INCOMPLETE (API defined, implementation missing)

## Description

Implement multi-hop relay paths for reachability when direct connection and hole punching fail. This is the **final fallback** in the connection ladder.

## Design

**NOT onion routing!** Static paths with:
- Dynamic discovery (find relays via DHT)
- Stable for session lifetime (not rebuilt)
- End-to-end encryption (relay cannot read payload)
- Performance-first (minimal overhead)

```
Initiator → Relay1 → Relay2 → Target

Path selection:
  1. Query DHT for target's relay hints
  2. Select relays based on:
     - Capability (NORN_EP_CAP_RELAY)
     - Uptime/latency
     - Number of hops (minimize)
  3. Path is STABLE for session
  4. Not rebuilt per packet
```

## API Defined

```c
// src/libnorn/norn_relay.h
typedef struct {
    norn_relay_hint_t hops[NORN_RELAY_MAX_HOPS];  // Up to 4 relays
    int hop_count;
    uint8_t session_id[NORN_RELAY_SESSION_ID_LEN];
} norn_relay_path_t;

// Path discovery
int norn_relay_discover_path(norn_client_t *client,
                              const uint8_t *target_pubkey,
                              norn_relay_path_t *path);

// Connection via path
int norn_relay_connect_path_async(norn_client_t *client,
                                   const uint8_t *target_pubkey,
                                   const norn_relay_path_t *path,
                                   norn_session_callback_t callback,
                                   void *user_data);
```

## Current Implementation

- Wire protocol: ✅ Complete (RelayCreate/Forward/Accept/Close)
- Session management: ✅ Complete (norn_relay_session_t)
- Path discovery: ❌ Missing
- Path connection: ❌ Missing
- Integration: ❌ Missing

## Missing Parts

### 1. Path Discovery
```c
int norn_relay_discover_path(norn_client_t *client,
                              const uint8_t *target_pubkey,
                              norn_relay_path_t *path) {
    // TODO:
    // 1. Query DHT for target's endpoint
    // 2. Extract relay hints from endpoint->payload
    // 3. Select optimal path (min hops, low latency)
    // 4. Return path
}
```

### 2. Path Connection
```c
int norn_relay_connect_path_async(...) {
    // TODO:
    // 1. Send RelayCreate to first hop
    // 2. Each relay forwards to next hop
    // 3. Final relay connects to target
    // 4. Target sends RelayAccept back through chain
    // 5. Session established
}
```

### 3. Connection Ladder Integration
```c
// src/libnorn/norn_session.c:140
if (endpoint->caps & NORN_EP_CAP_RELAY) {
    // TODO: Extract relay hints from endpoint->payload
    // TODO: Call norn_relay_connect_path_async()
}
```

## Wire Protocol (Already Complete)

```
RelayCreate (0x20):
  msg_type: 1 byte
  target_pubkey: 32 bytes
  session_id: 16 bytes
  signature: 64 bytes
  Total: 113 bytes

RelayForward (0x21):
  msg_type: 1 byte
  session_id: 16 bytes
  payload_len: 2 bytes
  payload: up to 1400 bytes (end-to-end encrypted)

RelayAccept (0x22):
  msg_type: 1 byte
  session_id: 16 bytes
  initiator_pubkey: 32 bytes
  signature: 64 bytes
  Total: 113 bytes
```

## Integration Points

- Called from `on_endpoint_resolved()` when direct and hole punch fail
- Path stored in endpoint payload
- Wire up `DIAL_RELAY` state in dial state machine

## Priority: High

This is the final fallback for NAT traversal - critical for reachability.

## Estimated Effort: 3-4 days

## Related: FEAT-017 (NAT Traversal)