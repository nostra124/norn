# norn NAT Traversal Design

## Design Philosophy

**Key Insight:** norn is for **reachability**, not anonymity.

- **Trust model:** We trust our peers (like the normal web)
- **Encryption:** End-to-end between session endpoints
- **Relays:** Know source and destination (not anonymous)
- **Performance:** First priority (low latency)
- **Path stability:** Dynamic discovery, but static for session lifetime

## NAT Traversal Ladder

```
norn_dial_async(pubkey)
  │
  ├─ Step 1: Resolve endpoint (DHT)
  │   - Query DHT for signed endpoint record
  │   - Extract: {pubkey, ip, port, caps, relay_hints[]}
  │
  ├─ Step 2: Try UPnP/NAT-PMP (automatic)
  │   - Attempt to create port mapping
  │   - If success: become publicly reachable
  │   - Direct connection works
  │
  ├─ Step 3: Try direct connection
  │   - If endpoint.ip != 0 (public IP):
  │     - Connect directly
  │   - Success: Session established ✓
  │   - Fail: Continue to Step 4
  │
  ├─ Step 4: Try hole punch (introducer)
  │   - Use rendezvous node (introducer)
  │   - Both sides send simultaneous probes
  │   - NAT creates mappings
  │   - Success: Session established ✓
  │   - Fail: Continue to Step 5
  │
  └─ Step 5: Use relay path (multi-hop)
      - Discover relay path: Relay1 → Relay2 → Target
      - Path is stable for session
      - Connect through relay chain
      - Success: Session established ✓
      - Fail: callback(NORN_SESSION_CLOSED)
```

## Introducers (Rendezvous)

**Concept:** Public node that coordinates hole punching between NAT'd peers.

```
Peer A (NAT)          Introducer (Public)          Peer B (NAT)
    │                        │                           │
    │── HolePunchReq ──────>│                           │
    │   (target=B)           │                           │
    │                        │── HolePunchReq ──────────>│
    │                        │   (target=A)              │
    │                        │                           │
    │<── HolePunchResp ──────│─── HolePunchResp ─────────│
    │   (B's IP:port)        │   (A's IP:port)           │
    │                        │                           │
    │── UDP probe ───────────┼── UDP probe ─────────────>│
    │<── UDP probe ──────────┼── UDP probe ──────────────│
    │                        │                           │
    │── Session INIT ───────>│<─── Session INIT ─────────│
    │                        │                           │
```

**In norn:** `norn_rendezvous` IS an introducer.

## UPnP/NAT-PMP

**Purpose:** Automatically create port mapping on router.

```
norn (behind NAT) ──► Router (UPnP/NAT-PMP) ──► Public Internet
                           │
                    "Map port 12345 to me"
                           │
                    ← External:port mapping
```

**When to use:**
- First choice before hole punching
- Works on most home routers
- Makes node publicly reachable
- Reduces need for relays

**API:**
```c
// Try automatic port mapping
norn_upnp_result_t result;
if (norn_auto_port_mapping(internal_port, "UDP", &result) == 0) {
    // Now publicly reachable on result.external_ip:result.external_port
}
```

## Multi-Relay with Static Paths

**Concept:** Use multiple relays in sequence (NOT onion routing).

```
Initiator ──► Relay1 ──► Relay2 ──► Target

Path Discovery:
  1. Query DHT for target's relay hints
  2. Select relays based on:
     - Capability (NORN_EP_CAP_RELAY)
     - Uptime/latency
     - Number of hops (minimize)
  3. Path is STABLE for session lifetime
  4. Not rebuilt per packet
  5. Not anonymous (each relay knows prev/next)
```

### Static vs. Onion Routing

| Aspect | norn (Static Path) | Tor/i2p (Onion Routing) |
|--------|-------------------|------------------------|
| **Path lifetime** | Session (minutes-hours) | 10 minutes (rebuilt) |
| **Path discovery** | Dynamic, then static | Continuous rebalancing |
| **Encryption** | End-to-end (1 layer) | Layered (1 layer per hop) |
| **Anonymity** | None (relay sees all) | Strong (hop knows only prev/next) |
| **Performance** | Low latency (~20-100ms) | High latency (~200-800ms) |
| **Trust required** | Yes (friend nodes) | No (zero-trust) |

### Path Discovery Algorithm

```c
// Example: Discover path to target
norn_relay_path_t path;
norn_relay_discover_path(client, target_pubkey, &path);

// Path contains:
path.hop_count = 2;  // Relay1 -> Relay2
path.hops[0] = {relay1_pubkey, relay1_ip, relay1_port};
path.hops[1] = {relay2_pubkey, relay2_ip, relay2_port};

// Connect through path
norn_relay_connect_path_async(client, target_pubkey, &path, callback, NULL);
```

### Wire Protocol (Multi-Hop)

```
RelayCreate (sent to Relay1):
  msg_type: 0x20
  target_pubkey: Final destination
  session_id: Random 16 bytes
  signature: Signed by initiator

RelayCreate (forwarded to Relay2):
  msg_type: 0x20
  target_pubkey: Final destination
  session_id: Same session ID
  signature: Signed by initiator

RelayForward (data):
  msg_type: 0x21
  session_id: Session identifier
  payload_len: uint16_t
  payload: Encrypted for target (end-to-end)

Each relay:
  - Looks up session by session_id
  - Forwards to next hop (Relay1 → Relay2 → Target)
  - Does NOT decrypt payload
```

## Security Model

### Trust Assumptions

```
norn:
  Trust: Friend nodes (pre-shared keys or DHT-signed)
  Threat: External passive attackers
  Protection: End-to-end encryption
  Vulnerability: Traffic analysis, malicious relay

i2pd/Tor:
  Trust: Zero-trust network
  Threat: Global active adversary
  Protection: Layered encryption + traffic mixing
  Resistance: Traffic analysis, correlation attacks
```

### End-to-End Encryption

```
Session establishment:
  1. Initiator generates ephemeral key pair
  2. Initiator → Target: ECDH(initiator_ephemeral, target_static)
  3. Derive shared secret
  4. All relay traffic encrypted with shared secret

Relay sees:
  - Source IP (initiator)
  - Destination IP (next hop)
  - Encrypted payload
  - Session ID
  
Relay CANNOT see:
  - Final destination pubkey
  - Application data
  - Other hop IPs (unless it's next to target)
```

## Endpoint Record Format

```c
// DHT-signed endpoint record
norn_endpoint_t ep = {
    .pubkey = peer_ed25519_pubkey,
    .ip = public_ip_or_zero,  // Zero if behind NAT
    .port = public_port_or_zero,
    .caps = NORN_EP_CAP_DIRECT 
          | NORN_EP_CAP_RENDEZVOUS 
          | NORN_EP_CAP_RELAY
          | NORN_EP_CAP_DHT,
    .payload = {
        // Optional: relay hints
        relay1_pubkey, relay1_ip, relay1_port,
        relay2_pubkey, relay2_ip, relay2_port,
        // Application-specific data
        ...
    }
};
```

## Comparison with Other Systems

| Feature | norn | i2pd | Tor |
|---------|------|------|-----|
| **Primary use** | Reachability | Anonymity | Anonymity |
| **Trust model** | Friend nodes | Zero-trust | Zero-trust |
| **Anonymity** | None | Strong | Strong |
| **Latency** | Low (10-100ms) | Medium (200-400ms) | High (500-1000ms) |
| **Path selection** | Static | Dynamic | Dynamic |
| **Encryption layers** | 1 (end-to-end) | 2-3 per tunnel | 3 |
| **Metadata exposure** | Full | Minimal | Minimal |
| **Connection setup** | < 1 second | 5-30 seconds | 10-60 seconds |
| **Use case** | F2F, IoT, gaming | Privacy apps | Anonymous browsing |

## Summary

**norn provides:**
1. **UPnP/NAT-PMP:** Automatic port forwarding (first choice)
2. **Introducers (Rendezvous):** Coordinated hole punching
3. **Multi-relay paths:** Stable paths through trusted nodes
4. **End-to-end encryption:** Relay cannot read payload
5. **Performance-first:** Low latency, trusted nodes

**norn does NOT provide:**
- Anonymity (relays know source/dest)
- Traffic analysis resistance
- Path rebuilding (static for session)
- Protection against malicious relays

**Key insight:** norn is for **friend-to-friend** networks where trust exists and performance matters more than anonymity.