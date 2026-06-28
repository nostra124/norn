# v0.10.0 — Private Overlay

Private mesh formation for fleet deployment.

## Status (2026-06-28)

**DONE for 1.0 (acceptance #1)** — acceptance #2 (NAT'd member via rendezvous/
relay) deferred post-1.0 with FEAT-022. Config API + docs + formation are
integration-tested (`tests/test_overlay_net.c`).

## Tickets

| ID | Title | Priority | Depends On | Status |
|----|-------|----------|------------|--------|
| FEAT-020 | Private overlay bootstrap | medium | FEAT-014, FEAT-016 | done (1.0 scope) |

## Overview

Closed fleets (e.g., an organization's regin + dvalin + raven agents) need
pubkey-addressed connectivity without announcing on the public mainline DHT.

`norn_config_t` already has `private_mode` + `boot_*`; this milestone hardens it
into a first-class "private overlay" story.

## Key Features

### Private Overlay Mode

**Configuration:**
```c
norn_config_t cfg = {
    .private_mode = 1,
    .boot_ips = fleet_bootstrap_ips,
    .boot_ports = fleet_bootstrap_ports,
    .boot_count = fleet_bootstrap_count,
};
```

**Behavior:**
- Form a private overlay from fleet bootstrap nodes
- No public mainline DHT announce
- Use native parameterised Kademlia (FEAT-014) + rendezvous/relay (FEAT-017)
- NAT'd/containerized agents are reachable inside mesh

### Bootstrap Flow

```
┌─────────────────────────────────────────────────────────────┐
│  Private Fleet                                              │
│                                                             │
│  ┌─────────┐     ┌─────────┐     ┌─────────┐               │
│  │ Node A  │←───│ Node B  │←───│ Node C  │               │
│  │(bootstrap)│   │(member) │   │(member) │               │
│  └────┬────┘     └────┬────┘     └────┬────┘               │
│       │               │               │                    │
│       └───────────────┼───────────────┘                    │
│                       │                                    │
│              Private Overlay                               │
│         (no public DHT announce)                           │
└─────────────────────────────────────────────────────────────┘
```

1. Bootstrap node provides initial peer list
2. New nodes join via configured bootstrap endpoints
3. Nodes discover each other by pubkey via private Kademlia
4. NAT traversal (hole-punch/relay) works inside mesh

## Architecture

```
Private Overlay Stack
├── norn_config_t.private_mode = true
├── Native Kademlia (FEAT-014)
│   └── Custom ID width (e.g., 264-bit for wyrd)
├── Dial by Pubkey (FEAT-016)
│   └── norn_dial(pubkey) → session
├── NAT Traversal (FEAT-017)
│   ├── Hole-punch via fleet rendezvous
│   └── Relay via fleet relay nodes
└── Application Layer
    ├── regin (fleet coordinator)
    ├── dvalin (state machine)
    └── raven (agent)
```

## Acceptance Criteria

1. Fleet of ≥3 nodes forms private overlay from single bootstrap node
2. Nodes resolve each other by pubkey via private Kademlia
3. NAT'd member is reachable via rendezvous/relay inside overlay
4. Zero traffic to public mainline DHT
5. Mode documented in `docs/PRIVATE-OVERLAY.md`

## Use Cases

1. **Enterprise fleet** — regin → dvalin → raven agents within organization
2. **Private packs** — wyrd private packs/clans
3. **Development clusters** — local test networks

## Cross-Repo

- regin, dvalin, raven — primary deployment mode for agent fleet
- wyrd — private packs/clans mode

## Related Milestones

- **v0.7.0**: Crypto suite (enables custom ID widths)
- **v0.8.0**: Dial & Session (enables connect by pubkey)
- **v0.9.0**: Tunnel & Bindings (same infrastructure)