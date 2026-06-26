---
id: FEAT-024
type: feature
priority: medium
complexity: L
estimate_tokens: 120k-240k
estimate_time: 180-360min
phase: build
status: open
depends_on: []
milestone: MILESTONE-0.11.0
spawned_from: ~
---
# Pure Raft consensus core (`norn_raft`) — PreVote, learners, candidacy hook

## Description

**As a** cluster layer that needs replicated consensus
**I want** a transport- and clock-agnostic Raft state machine
**So that** consensus logic is provably correct and 100% unit-testable without
a network.

This is the keystone of MILESTONE-0.11.0 (clustered KV store). It implements
the Raft algorithm as a pure state machine: it is fed inbound messages and a
monotonic `tick(now_ms)`, and it emits actions ("send RPC X to peer P", "apply
committed entry E") through a callback vtable. No sockets, no clock, no
allocation in the step path beyond a bounded log/arena.

The heterogeneous-membership support (servers lead, phones are learners) lives
here as two **liveness-only, safety-preserving** features: the voter/learner
distinction and a candidacy-eligibility predicate. See
[`docs/cluster-kv.md`](../../../docs/cluster-kv.md).

## Implementation

- `src/libnorn/norn_raft.{c,h}`: state (currentTerm, votedFor, log[],
  commitIndex, lastApplied, role ∈ {follower, candidate, leader, learner}),
  and the RPC handlers:
  - **RequestVote / PreVote** — PreVote (Ongaro §9.6) gates a real term bump so
    flaky members can't disrupt a healthy leader.
  - **AppendEntries** — log matching, conflict truncation, commit advance,
    heartbeats; carries the leader's class/uptime for follower bookkeeping.
  - **InstallSnapshot** — log compaction support (apply-side snapshot is
    FEAT-026; the core handles the protocol + log truncation).
  - **TimeoutNow** — leadership transfer target (Ongaro §3.10).
- **Learners (non-voting members):** receive AppendEntries, advance commit, but
  are excluded from quorum counting and never grant/seek votes.
- **Candidacy-eligibility hook:** a follower transitions to candidate only if a
  supplied predicate returns true (default: voter && election_eligible). This
  is how "only proven-uptime servers may lead" is expressed without touching
  voting or quorum math.
- **Membership:** single-server add/remove and learner→voter promotion
  (Ongaro §4.1), applied as special log entries.
- Vtable for effects: `send(peer, msg, len)`, `apply(entry)`,
  `save_state(term, votedFor)` (persistence is provided by the caller;
  in-memory for tests).
- Deterministic: all timing via injected `now_ms`; all randomness (election
  timeout jitter) via an injected seed/callback, so tests are reproducible.

## Acceptance Criteria

1. Three in-memory cores elect a single leader, replicate entries, and agree on
   commit order under a simulated network (drop/reorder/delay).
2. A core flagged non-eligible (e.g. class != SERVER) **never** becomes leader,
   even if its election timer fires.
3. Learners receive and apply all committed entries but are excluded from
   quorum: a 3-voter + N-learner cluster commits with 2 voters and 0 learners.
4. PreVote prevents a partitioned-then-rejoined node from forcing a new
   election when the current leader is healthy.
5. Single-server add/remove and learner→voter promotion preserve safety under
   the test harness.
6. **100% line and branch coverage** (`make coverage`), per AGENTS.md.

## Cross-repo

Foundation for the fleet's pubkey-addressed replicated config/state store
(regin, thunder, dvalin, raven). Reusable by any consumer needing consensus.

## Progress (2026-06-26) — core implemented + tested; branch-coverage polish remains

`src/libnorn/norn_raft.{c,h}` implements the pure, I/O-free state machine fed by
`norn_raft_step(msg)` + `norn_raft_tick(now_ms)`, emitting effects through a
vtable (`send`/`apply`/`save_state`/`eligible`). `tests/test_raft.c` is a
deterministic in-memory multi-core simulation (seeded jitter, a controllable
message bus). Built under the project's `-Wall -Wextra -Werror -std=c99`; 21
tests pass.

Done:
- ✅ AC1 — 3 voters elect a single leader, replicate, and agree on commit order.
- ✅ AC2 — a non-eligible voter never leads (candidacy-eligibility predicate);
  proven by a cluster where elections only start once a node is made eligible.
- ✅ AC3 — learners get the full log + apply committed entries but are excluded
  from quorum (a 3-voter + 2-learner cluster commits on 2 voters).
- ✅ AC4 — PreVote: a node that still hears a healthy leader refuses pre-votes,
  so a flapping member never forces a term bump.
- ✅ AC5 — single-server add/remove and learner→voter promotion (Ongaro §4.1),
  applied on append.
- ✅ §5.4.2 safety: a new leader does not commit a prior-term entry by count.
- ✅ Coverage: **100% line, 100% function** (`make coverage` via lcov, honoring
  the `LCOV_EXCL` markers on genuinely-unreachable defensive paths).

Remaining (keeps the ticket open):
- ⚠️ AC6 — **branch coverage is 87.6% (254/290), not the required 100%.** ~36
  both-direction branch cases (mostly compound-condition sides) remain; closing
  them is mechanical test-writing on the existing harness.
- ⚠️ InstallSnapshot **wire protocol**: local log compaction (`norn_raft_snapshot`)
  is implemented + tested, but sending/receiving a snapshot to catch up a far-
  behind follower is not yet wired (the design lists it under FEAT-024; the
  apply-side snapshot is FEAT-026).

Build note: in a strict-`c99` environment the existing `norn.c` needs
`-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE` for `strdup`/`clock_gettime`
(pre-existing; unrelated to this module, which is clean ISO C99).
