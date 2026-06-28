---
id: FEAT-014
type: feature
priority: high
complexity: L
estimate_tokens: 100k-200k
estimate_time: 120-240min
phase: done
status: done
depends_on: [FEAT-013]
milestone: MILESTONE-0.7.0
spawned_from: ~
---
# Parameterise Kademlia node-id width / keyspace

## Description

**As a** norn integrator with non-20-byte identities (wyrd: wider keyspace)
**I want** the Kademlia routing table parameterised on node-id width
**So that** an overlay can use any id length while bifrost keeps 20-byte
mainline ids.

`NORN_ID_BYTES` is currently hardcoded at 20. XOR distance, bucket indexing
and leading-zero counting must generalise to `suite->nodeid_len` bytes.

## Implementation

- Drive routing-table id length from `norn_crypto_suite_t.nodeid_len`
  (FEAT-013); generalise XOR distance / bucket math to N bytes.
- **Two cleanly separated DHT pieces:**
  - *Public mainline / BEP-5/44 client* — stays **fixed 20-byte, Ed25519** by
    external spec; used for interop + bootstrap.
  - *Native parameterised Kademlia overlay* — width from the suite; norn-native
    wire format.
- bifrost rides mainline *as* its overlay (ids already 20-byte). wyrd runs the
  native overlay (wider) and uses mainline for bootstrap only (Decision 2a).

## Acceptance Criteria

1. Routing table, distance and bucket selection work for ≥20-byte ids.
2. Mainline client remains 20-byte/Ed25519 and BEP-44-interop-tested.
3. A test instantiates the overlay at a non-20-byte width and exercises
   insert / closest-N / distance ordering.

## Cross-repo

Unblocks wyrd FEAT-293 (264-bit pubkey-as-id overlay on norn).
