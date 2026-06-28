---
id: FEAT-017
type: feature
priority: high
complexity: XL
estimate_tokens: 200k-400k
estimate_time: 240-480min
phase: done
status: done
depends_on: [FEAT-013, FEAT-016]
milestone: MILESTONE-0.8.0
spawned_from: ~
---
# Harmonised NAT traversal — rendezvous hole-punch + onion relay

## Description

**As an** application dialing a peer behind NAT
**I want** norn to hole-punch and, failing that, relay — transparently
**So that** `norn_dial(pubkey)` works through cone, symmetric and CGNAT
without the app knowing.

This is currently **duplicated**: bifrost has `circuit` (onion) + BEP-55
rendezvous; wyrd has `relay_onion` (FEAT-114) + holepunch coordination
(FEAT-115). One crypto-agnostic implementation replaces both (Q2 decision).

## Implementation

- **Rendezvous hole-punch:** a mutually-reachable peer signals both sides to
  fire probes simultaneously (BEP-55-style; harmonise bifrost's rendezvous and
  wyrd's FEAT-115 coordination message).
- **Onion relay fallback:** fixed-size padded cells, layered AEAD per hop, all
  via the crypto suite (FEAT-013); harmonise bifrost `circuit` + wyrd
  `relay_onion` (FEAT-114). Relays are untrusted and end-to-end encrypted.
- Wired into the `norn_dial` ladder: direct → hole-punch → relay.
- Relay/rendezvous *capability advertisement* is an opaque app cap
  (FEAT-015) — norn provides the mechanism, the app decides who relays.

## Acceptance Criteria

1. Two peers behind simulated symmetric NAT establish a session via rendezvous.
2. When hole-punch fails, the dial falls back to a relayed circuit; payload is
   confidential to the relay.
3. Cell layout / overhead documented; works under any installed suite.

## Cross-repo

Retires bifrost `circuit`/BEP-55 (FEAT-080) and wyrd FEAT-114/FEAT-115
duplicates (wyrd FEAT-294).
