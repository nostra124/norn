# MILESTONE 0.11.0 — CLUSTERED KEY-VALUE STORE (class-aware Raft)

**Goal:** Add a new top-level capability to libnorn: a **replicated key-value
store shared across a cluster of nodes** — "etcd over libnorn". Nodes form a
cluster, others join and leave, and every member holds a consistent replica.
Members are addressed by **public key**, so the cluster rides norn's existing
discovery, NAT traversal and end-to-end encryption (`norn_dial(pubkey) →
session → stream`).

**The twist — heterogeneous availability.** Members belong to classes with very
different uptime: phones, laptops, workstations, servers. Classic Raft needs a
majority of *all* members online; that would stall the moment phones sleep.
This milestone delivers **class-aware Raft** where only proven-uptime **servers
carry the consensus quorum and can be elected leader**, while phones/laptops
join as **learners** that hold a full replica but never block progress or lead.

This requires **no change to Raft's safety core** — the heterogeneity is
expressed through two liveness-only, safe-by-construction mechanisms:
voter/learner membership, and a candidacy-eligibility predicate
(`class == SERVER && uptime ≥ threshold`) plus PreVote. See
[`docs/cluster-kv.md`](../docs/cluster-kv.md) for the full design and the
rationale on why this is safe.

## Background

norn already provides everything the cluster transport needs:

- `norn_dial(pubkey) → session` with multiplexed reliable streams (FEAT-016),
- harmonised NAT traversal so NAT'd members are dialable (FEAT-017),
- crypto-agnostic encrypted channels and DHT discovery.

So the cluster layer is purely *consensus + replication + class-aware
membership* on top — no new transport, crypto or NAT work.

## Settled design decisions

1. **Voters = servers, Learners = everyone else.** Quorum is computed over the
   voter set only, so cluster progress depends on a majority of *servers*, not
   of all members. Learners get the full log and serve reads.
2. **Candidacy eligibility, not vote restriction.** "Only a proven-uptime
   server may lead" is enforced by gating the follower→candidate transition on
   a pluggable predicate, plus **PreVote**. Voting rules and quorum math are
   left unmodified, so Raft's safety proofs hold unchanged.
3. **Learner-first joins, single-server membership changes.** New members join
   as learners and catch up; servers are then promoted learner→voter. Removal
   demotes then removes one member at a time (Ongaro §4.1).
4. **Pure, I/O-free Raft core.** Consensus lives in `norn_raft.c`, fed messages
   + `tick(now_ms)` and emitting send/apply via a vtable, so it is 100%
   unit-testable in isolation (per AGENTS.md coverage policy).
5. **Policy stays in the app.** Consistent with MILESTONE-0.3.0: norn provides
   consensus/replication mechanics; *who may join/write* is the application's
   concern, enforced by which pubkeys it admits.

## Features

| id | title | status |
|----|-------|--------|
| FEAT-024 | Pure Raft consensus core (`norn_raft`) — PreVote, learners, candidacy hook | [~] in build — core + 24 tests; 100% line/func, 90.3% branch (coverage polish + InstallSnapshot wire remain) |
| FEAT-025 | Cluster ↔ session glue (`norn_cluster`) — RPC over norn streams, membership, timers | [ ] planned |
| FEAT-026 | Replicated KV state machine (`norn_kvstore`) + class-aware membership API | [ ] planned |

## Suggested order

FEAT-024 is the keystone — a transport-free, fully tested Raft state machine
(elections, log replication, PreVote, learner promotion, candidacy predicate,
snapshots). FEAT-025 binds it onto norn sessions (bencode RPCs, timers off
`norn_tick`, pubkey→session map, membership config). FEAT-026 adds the KV
semantics (apply, linearizable ReadIndex reads, watches, CAS, snapshots) and
surfaces the public class-aware membership + KV API.

## Acceptance (milestone-level)

1. Two server nodes **bootstrap** a cluster; a third node **joins** as a
   learner, catches up, and (if a server) is **promoted** to voter.
2. A `kv_put` on any member commits and becomes visible (consistently) on all
   members, including learners.
3. With phones/laptops as learners, **putting a majority of learners offline
   does not stall** writes as long as a majority of servers is up.
4. A non-server member **never wins an election**; PreVote prevents a flaky
   member from disrupting a healthy leader.
5. The pure Raft core (FEAT-024) has **100% line and branch coverage**.

## Consumer-side counterparts

The fleet's coordination needs (regin's agent registry, thunder's shared
config, dvalin/raven state) are day-1 consumers of a pubkey-addressed
replicated KV store that tolerates mostly-offline edge members.
