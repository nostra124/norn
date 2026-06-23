---
id: FEAT-020
type: feature
priority: medium
complexity: M
estimate_tokens: 60k-120k
estimate_time: 90-180min
phase: planned
status: open
depends_on: [FEAT-014, FEAT-016]
milestone: MILESTONE-0.10.0
spawned_from: ~
---
# Private-overlay bootstrap / fleet rendezvous

## Description

**As a** closed fleet (e.g. one org's regin + dvalin + raven agents)
**I want** to form a private pubkey-mesh from my own bootstrap node(s)
**So that** I get pubkey-addressed connectivity without announcing on the
public mainline DHT.

`norn_config_t` already has `private_mode` + `boot_*`; this hardens it into a
first-class "private overlay" story.

## Implementation

- Clean config path: "here are my fleet bootstrap pubkeys/endpoints, form a
  private overlay" — no public-mainline announce in this mode.
- Private overlay uses the native parameterised Kademlia (FEAT-014) + the
  rendezvous/relay from FEAT-017 so NAT'd / containerised agents are dialable
  inside the mesh.
- Document the public-mainline-bootstrap vs private-overlay modes and when to
  use each.

## Acceptance Criteria

1. A fleet of ≥3 nodes forms a private overlay from a single bootstrap node,
   resolves each other by pubkey, and never touches the public DHT.
2. A NAT'd member is reachable via rendezvous/relay inside the private overlay.
3. Mode documented in `docs/`.

## Cross-repo

The deployment mode for the agent fleet (regin → dvalin → raven) and for
wyrd's private packs/clans.
