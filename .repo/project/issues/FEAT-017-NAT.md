# FEAT-017 NAT Traversal

## Overview

Implement NAT traversal for peer-to-peer connections when peers are behind NAT. The implementation uses a ladder approach: direct → hole-punch → relay.

## Architecture

### Connection Ladder

```
norn_dial_async(pubkey)
    │
    ├─→ Step 1: Resolve endpoint (DHT signed record)
    │   - norn_resolve_endpoint_async()
    │   - Get {pubkey, ip, port, payload}
    │
    ├─→ Step 2: Try direct connection
    │   - If peer.ip != 0 (public IP):
    │     - norn_dial_direct_async(endpoint)
    │   - Success: Session established ✓
    │   - Fail: Continue to Step 3
    │
    ├─→ Step 3: Hole punching (rendezvous)
    │   - Find mutual rendezvous peer via DHT
    │   - Send punch request to rendezvous
    │   - Both sides send probes simultaneously
    │   - Success: Session established ✓
    │   - Fail: Continue to Step 4
    │
    └─→ Step 4: Relay fallback
        - Connect to relay (from peer endpoint or DHT)
        - Establish relayed circuit
        - Success: Session established ✓
        - Fail: callback(NORN_SESSION_CLOSED)
```

### NAT Types

```
Type 1: Public IP (no NAT)
    - Direct connection works
    - 10% of Internet

Type 2: Full-cone NAT (easy)
    - Hole punching works
    - 20% of NATs

Type 3: Restricted-cone NAT (medium)
    - Hole punching works with rendezvous
    - 30% of NATs

Type 4: Symmetric NAT (hard)
    - Requires relay
    - 40% of NATs
```

## API Design

### Hole Punching

```c
/**
 * @brief Request hole punch via rendezvous
 *
 * Sends punch request to rendezvous peer, which signals both sides
 * to send simultaneous probes.
 *
 * @param client Client handle
 * @param target_pubkey Peer to connect to
 * @param rendezvous_pubkey Rendezvous peer (must be publicly reachable)
 * @param callback State change callback
 * @param user_data User data
 * @return 0 on success, -1 on error
 */
int norn_hole_punch_async(norn_client_t *client,
                          const unsigned char *target_pubkey,
                          const unsigned char *rendezvous_pubkey,
                          norn_session_callback_t callback,
                          void *user_data);

/**
 * @brief Act as rendezvous for hole punching
 *
 * When two peers want to connect, both send punch requests to this client.
 * This client signals both to send probes at the same time.
 *
 * @param client Client handle (must be publicly reachable)
 * @param callback Called when peers need coordination
 * @param user_data User data
 * @return 0 on success, -1 on error
 */
int norn_rendezvous_enable(norn_client_t *client,
                          void *callback,
                          void *user_data);
```

### Relay

```c
/**
 * @brief Connect via relay
 *
 * Establishes a relayed connection through a relay peer.
 * Traffic is encrypted end-to-end; relay cannot see content.
 *
 * @param client Client handle
 * @param target_pubkey Peer to connect to
 * @param relay_pubkey Relay peer (must be publicly reachable)
 * @param callback State change callback
 * @param user_data User data
 * @return 0 on success, -1 on error
 */
int norn_relay_connect_async(norn_client_t *client,
                             const unsigned char *target_pubkey,
                             const unsigned char *relay_pubkey,
                             norn_session_callback_t callback,
                             void *user_data);

/**
 * @brief Act as relay
 *
 * Forwards traffic between two peers. Cannot see encrypted payload.
 *
 * @param client Client handle (must be publicly reachable)
 * @param callback Called when relay circuit is requested
 * @param user_data User data
 * @return 0 on success, -1 on error
 */
int norn_relay_enable(norn_client_t *client,
                      void *callback,
                      void *user_data);
```

## Wire Protocol

### Hole Punch Request (via DHT)

```
HolePunchRequest {
    uint8_t msg_type = 0x10;           // BEP-44 extension
    uint8_t target_pubkey[32];          // Who to connect to
    uint8_t my_ephemeral_pubkey[32];    // For this session
    uint32_t my_external_ip;            // My external IP (learned from STUN)
    uint16_t my_external_port;          // My external port
    uint8_t signature[64];               // Signed by my identity
}
```

### Hole Punch Response (from rendezvous)

```
HolePunchResponse {
    uint8_t msg_type = 0x11;
    uint8_t peer_pubkey[32];            // The peer's identity
    uint32_t peer_external_ip;          // Peer's external IP
    uint16_t peer_external_port;         // Peer's external port
    uint8_t peer_ephemeral_pubkey[32];   // Peer's ephemeral key
    uint8_t signature[64];              // Signed by rendezvous
}
```

### Relay Circuit Setup

```
RelayCreate {
    uint8_t msg_type = 0x20;
    uint8_t circuit_id[16];             // Random circuit ID
    uint8_t target_pubkey[32];          // Final destination
    uint8_t my_ephemeral_pubkey[32];    // For layered encryption
    uint8_t signature[64];              // Signed by my identity
}

RelayExtend {
    uint8_t msg_type = 0x21;
    uint8_t circuit_id[16];
    uint8_t hop_pubkey[32];             // Next hop
    uint8_t encrypted_payload[...];     // Layered AEAD
}
```

## Implementation Plan

### Phase 1: Endpoint Discovery (2 days)
- Implement `norn_resolve_endpoint_async()`
- Query DHT for signed endpoint record
- Parse payload for capabilities (relay, rendezvous)
- Add endpoint cache with TTL

### Phase 2: Direct Connection (1 day)
- Extend `norn_dial_async()` to try direct first
- If endpoint has public IP, attempt direct connection
- Fall back to hole punch on failure

### Phase 3: Hole Punching (3 days)
- Implement `norn_hole_punch_async()`
- Implement rendezvous coordination
- Add STUN-like external IP discovery
- Implement simultaneous probe sending

### Phase 4: Relay Fallback (4 days)
- Implement `norn_relay_connect_async()`
- Implement relay circuit creation
- Implement layered encryption (onion routing)
- Implement relay enable/disable

### Phase 5: Integration (2 days)
- Integrate into `norn_dial_async()` ladder
- Add connection timeout handling
- Add fallback logic (direct → hole-punch → relay)
- Add metrics/logging

## Testing Strategy

### Unit Tests
- Hole punch message serialization
- Relay circuit creation
- Endpoint parsing

### Integration Tests (SIT)
- Direct connection (loopback)
- Hole punching (simulated NAT)
- Relay connection (three processes)

### Network Tests (PIT)
- Real NAT traversal over Internet
- Symmetric NAT fallback
- Relay performance

## Security Considerations

1. **Hole Punch Authentication**
   - All messages signed by identity key
   - Ephemeral keys for forward secrecy
   - Rendezvous cannot MITM (end-to-end encryption)

2. **Relay Privacy**
   - Relay cannot see payload (end-to-end encryption)
   - Circuit ID is random per session
   - Layered encryption for multi-hop

3. **DOS Prevention**
   - Rate limit hole punch requests
   - Rate limit relay circuit creation
   - Circuit timeout after inactivity

## References

- **BEP-55**: Holepunch extension for uTP
- **BEP-44**: Mutable items in DHT
- **STUN**: RFC 5389 (Session Traversal Utilities for NAT)
- **TURN**: RFC 8656 (Traversal Using Relays around NAT)
- **Onion Routing**: Tor protocol specification