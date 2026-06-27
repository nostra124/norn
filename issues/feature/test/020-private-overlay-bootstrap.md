---
id: FEAT-020
type: feature
priority: medium
complexity: M
estimate_tokens: 60k-120k
estimate_time: 90-180min
phase: test
status: in_progress
depends_on: [FEAT-014, FEAT-016]
blocks_acceptance_2_on: [FEAT-022]
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

## Implementation Status

- ✅ First-class private-overlay config API (`norn_overlay.{c,h}`): describe a
  fleet's bootstrap set as **pubkey + endpoint** peers, validate it (no
  zero/duplicate keys, bounded), and project it into a private-mode
  `norn_config_t` (`private_mode = 1`, `boot_*` filled). Pure and 100%
  unit-tested. Pubkeys also seed cluster membership (FEAT-025).
- ✅ Modes documented in [`docs/private-overlay.md`](../../../docs/private-overlay.md)
  (public-mainline bootstrap vs private overlay).
- ✅ **Acceptance #1 — overlay formation + pubkey resolution + no public-DHT
  pollution** — covered deterministically by `tests/test_overlay_net.c`
  (loopback, no network): a 3-node fleet forms a private overlay from a *single*
  bootstrap node, members resolve each other by their pubkey-derived DHT ids
  (a member learns a peer beyond the seed via referral), and every routing table
  stays **fleet-only** (public routers are never contacted under `private_mode`
  — `mainline_bootstrap` skips them — so they never enter the table). A private
  node with no fleet peers initiates nothing (no public fallback).
- ⏳ **Acceptance #2 — a NAT'd member reachable via rendezvous/relay inside the
  overlay** — rides FEAT-017's relay path, whose multi-hop integration is
  **FEAT-022 (still open)**, and needs real NAT topology. This is a PIT item and
  is what keeps FEAT-020 from `done`; it unblocks once FEAT-022 lands.
