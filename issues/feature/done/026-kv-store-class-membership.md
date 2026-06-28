---
id: FEAT-026
type: feature
priority: medium
complexity: L
estimate_tokens: 90k-180k
estimate_time: 150-300min
phase: done
status: done
depends_on: [FEAT-024, FEAT-025]
milestone: MILESTONE-0.11.0
spawned_from: ~
---
# Replicated KV state machine (`norn_kvstore`) + class-aware membership API

## Description

**As an** application (regin, thunder, dvalin, raven …)
**I want** a consistent replicated key-value store across a heterogeneous
cluster, addressed by public key
**So that** I get "etcd over libnorn" with edge members (phones/laptops) that
hold a full replica but never stall the cluster.

This feature delivers the replicated state machine that sits behind the Raft
core, plus the public class-aware membership + KV API that applications use.

## Implementation

- `src/libnorn/norn_kvstore.{c,h}`: deterministic replicated map applied from
  committed Raft entries:
  - operations **PUT / DEL / CAS** (compare-and-swap), encoded as log entries;
  - **linearizable reads** on the leader via **ReadIndex** (Ongaro §6.4) — no
    log write needed for a consistent read;
  - **bounded-stale local reads** on learners/followers for cheap reads on edge
    members;
  - **prefix watches** — emit add/update/delete events to subscribers as
    entries apply;
  - **snapshots** for log compaction (serialize map → snapshot; restore on
    InstallSnapshot), wired to the FEAT-024 snapshot protocol;
  - bounded memory (configurable byte budget, consistent with `dhtstore`).
- `src/libnorn/norn_cluster.h` public surface (class-aware):
  - `norn_node_class_t` { MOBILE, LAPTOP, WORKSTATION, SERVER } and
    `norn_cluster_config_t` (self_class, uptime_score, election_eligible);
  - `norn_cluster_new/free`, `bootstrap/join/leave/promote/demote`;
  - `kv_put / kv_cas / kv_del / kv_get / kv_watch`;
  - `norn_cluster_leader`, `norn_cluster_members`,
    `norn_cluster_set_role_eligibility`.
- Default policy: `SERVER` → voter + election-eligible; all other classes →
  learner. Overridable via the eligibility predicate.

## Acceptance Criteria

1. `kv_put` on any member commits and the value is consistently visible on all
   members, including learners.
2. With 3 servers (voters) + N phones (learners), taking **all** learners
   offline does **not** block writes; taking 2 of 3 servers offline does (as
   Raft requires).
3. `kv_cas` succeeds/fails atomically under concurrent proposals.
4. A `kv_watch` on a prefix fires for each matching committed change.
5. Snapshot + restore reproduces the exact map; a lagging learner catches up via
   InstallSnapshot after log compaction.
6. The state-machine module has **100% line and branch coverage**.

## Cross-repo

The fleet's shared-config / coordination primitive: a pubkey-addressed
replicated KV store that tolerates mostly-offline edge members — directly
consumable by regin (agent registry), thunder (shared config), dvalin/raven.
