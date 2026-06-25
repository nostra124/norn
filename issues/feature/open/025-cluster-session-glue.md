---
id: FEAT-025
type: feature
priority: medium
complexity: L
estimate_tokens: 120k-240k
estimate_time: 180-360min
phase: planned
status: open
depends_on: [FEAT-024, FEAT-016, FEAT-017]
milestone: MILESTONE-0.11.0
spawned_from: ~
---
# Cluster ↔ session glue (`norn_cluster`) — Raft RPC over norn streams

## Description

**As a** cluster of nodes addressed by public key
**I want** the pure Raft core (FEAT-024) bound onto live norn sessions
**So that** consensus messages flow over encrypted, NAT-traversing,
pubkey-addressed streams with no new transport code.

This layer is the bridge between the I/O-free Raft core and the rest of norn.
It owns the member↔session map, serializes RPCs, and drives Raft's timers from
the event loop.

## Implementation

- `src/libnorn/norn_cluster.{c,h}`:
  - **Member transport:** for each member pubkey, maintain a norn session via
    `norn_dial_async` (outbound) / `norn_listen_async` (inbound), reusing
    FEAT-016 sessions and FEAT-017 NAT traversal. One long-lived stream per
    peer carries Raft RPCs.
  - **RPC codec:** bencode-encode/decode RequestVote, PreVote, AppendEntries,
    InstallSnapshot, TimeoutNow (reuse `bencode.c`). Length-framed over the
    reliable stream.
  - **Timers off `norn_tick`:** translate wall-clock ticks into the core's
    `tick(now_ms)`; manage election timeout (with jitter) and heartbeat
    interval. No blocking, no threads — pure event-loop integration.
  - **Membership configuration:** `bootstrap(peers)`, `join(seed)` (as
    learner), `leave()`, `promote()/demote()`, each issued as the core's
    membership log entries; resolve seed/member endpoints by pubkey.
  - **Node class / eligibility policy:** hold `self_class`, `uptime_score`, and
    the eligibility predicate; feed it to the core's candidacy hook. Default:
    eligible iff `class == SERVER`.
  - **Write forwarding:** proposals issued on a follower/learner are forwarded
    to the current leader over its stream; surface the committed/redirect
    result to the caller.
- Persistence hook is caller-provided (in-memory default); the cluster layer
  wires the core's `save_state`/snapshot callbacks to it.

## Acceptance Criteria

1. Two nodes `bootstrap` a cluster over real norn sessions and elect a leader.
2. A third node `join`s by seed pubkey, establishes sessions to existing
   members, and receives the replicated log as a learner.
3. Raft RPCs round-trip correctly through the bencode codec over norn streams
   (unit test the codec independently to 100% coverage).
4. A write issued on a non-leader is forwarded and commits cluster-wide.
5. Losing/re-establishing a member's session is handled (reconnect, resend
   in-flight RPCs) without violating safety.

## Cross-repo

Makes the Raft core usable by the fleet without each app reinventing the
session-to-member plumbing.
