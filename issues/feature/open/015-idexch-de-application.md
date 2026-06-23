---
id: FEAT-015
type: feature
priority: high
complexity: L
estimate_tokens: 100k-200k
estimate_time: 120-240min
phase: planned
status: open
depends_on: [FEAT-013]
milestone: MILESTONE-0.7.0
spawned_from: ~
---
# De-application-ise `idexch` (opaque identity record + caps passthrough)

## Description

**As a** base-layer maintainer
**I want** `idexch` stripped of bifrost-specific concepts
**So that** norn's identity exchange is application-agnostic and DECISIONS #9
("siblings, no leaked app concepts") holds at the code level.

`idexch.h` currently carries `account`, VPN `ULA`, `CAP_VPN`/`CAP_RELAY`/… and
the `BFID` wire magic — all bifrost semantics living inside the shared lib.

## Implementation

- Reduce norn's identity exchange to its generic kernel: a **signed assertion**
  binding `pubkey + nonce + endpoint + opaque app payload`, verified via the
  suite (FEAT-013). norn does not parse the payload.
- Replace the typed capability bitfield with an **opaque, app-defined caps
  blob** carried in (and re-emitted from) the payload — norn passes it through,
  the application interprets it.
- Move bifrost-specific fields (`account`, `ULA`, `CAP_VPN`, …) and the `BFID`
  framing **out of libnorn into bifrost** (bifrost counterpart: FEAT-081).
- Keep the stateless single-round-trip shape and signature coverage.

## Acceptance Criteria

1. `idexch` in libnorn contains no `account`/`ULA`/`CAP_*`/`BFID` symbols.
2. The signed-assertion codec round-trips an arbitrary opaque payload.
3. bifrost's account/ULA/caps still work, now defined in bifrost (FEAT-081).

## Cross-repo

Paired with bifrost FEAT-081 (owns its identity record) and wyrd's HELLO /
trust-signal mapping onto the generic envelope.
