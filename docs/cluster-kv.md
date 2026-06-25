# Clustered Key-Value Store over libnorn (class-aware Raft)

> Status: **design** — see `issues/MILESTONE-0.11.0-CLUSTER-KV.md` and
> FEAT-024 / FEAT-025 / FEAT-026.

## Goal

"etcd over libnorn": let a set of nodes **form a cluster**, let others
**join/leave**, and keep a **replicated key-value store** consistent on every
member. Members are addressed by **public key** (not IP), so the cluster
inherits norn's discovery, NAT traversal and end-to-end encryption for free —
the cluster transport is just `norn_dial(pubkey) → session → stream`.

## The hard part: heterogeneous members

The members belong to different *classes* with very different availability:

| Class | Example | Availability |
|-------|---------|--------------|
| `NORN_NODE_MOBILE` | phone | rarely online, flaky, battery-bound |
| `NORN_NODE_LAPTOP` | laptop | intermittently online |
| `NORN_NODE_WORKSTATION` | desktop | mostly online, can reboot |
| `NORN_NODE_SERVER` | server / VPS | permanently online |

Classic Raft requires a **majority of all members** to be online to make
progress. If phones and laptops counted toward that majority, the cluster
would stall the moment enough of them went to sleep. The question is whether
Raft can be adapted so that **only proven-uptime servers carry consensus and
can be elected leader**, while the flaky members still get a full replica.

**Answer: yes — and this is exactly how etcd-class systems already handle
asymmetric fleets.** No change to Raft's *safety* core is needed. The
heterogeneity lives entirely in two liveness-only mechanisms that are safe by
construction:

### 1. Voters vs. Learners (the key idea)

Raft has a standard notion of **learners** (a.k.a. non-voting members — used
by etcd, TiKV, Consul). A learner:

- **receives the full replicated log** (so it holds a complete, consistent
  copy of the KV store and can serve reads),
- **does *not* count toward quorum**, and
- **cannot vote and cannot become leader**.

So we map the node classes onto roles:

- **Servers** → **voting members**. They alone form the consensus quorum.
- **Phones / laptops / workstations** → **learners**. They get a fully
  replicated copy, serve local reads, and forward writes to the leader, but
  they never block progress and never lead.

This directly answers *"majority of nodes must be online"*: **quorum is
computed only over the voter set (the servers)**. As long as a majority of
*servers* is up, the cluster commits writes — no matter how many phones are
asleep. A 3-server voter set tolerates 1 server down while serving an
arbitrary number of dozing learner phones.

### 2. Candidacy eligibility ("only a proven-uptime server may lead")

Even among voters you may want only stable servers to *lead*. The relevant
mechanisms, all liveness-only:

- **PreVote** (Ongaro thesis §9.6): a node verifies it *could* win an election
  before bumping its term. This stops a phone that keeps reconnecting from
  disrupting a healthy cluster with spurious term increments. Essential for
  flaky members.
- **Candidacy-eligibility predicate**: a member only ever transitions to
  *candidate* if `class == SERVER && uptime_score ≥ threshold`. Ineligible
  members simply never start an election. Learners already can't run; this
  additionally lets you keep a *voter* in "follower-only" mode.
- **CheckQuorum + leader lease**: a leader that cannot reach a quorum steps
  down, so a partitioned/own-going-offline server yields promptly.
- **Leadership transfer** (Ongaro §3.10 `TimeoutNow`): gracefully hand
  leadership to a specific target — used to move leadership onto the most
  stable server, or off a server about to be demoted.

> **Why this is safe.** Raft's election-safety proof never depends on a node
> *choosing to run*. Restricting *who becomes a candidate* can only reduce
> liveness, never violate safety — a node that declines to run can't split a
> vote or elect two leaders in a term. What you may **not** do casually is
> change *who votes* or *the quorum size*: that must go through Raft's
> membership-change protocol (below). "Proven uptime" is therefore an
> eligibility gate on **candidacy**, not on voting — which is exactly why it
> composes with unmodified Raft.

### 3. "Proven uptime" as a concrete signal

`uptime_score` is not a native Raft concept; it is an application input to the
eligibility predicate. Practical sources:

- measured session longevity (norn already tracks session lifetime),
- a monotonic "time online since last full restart" counter gossiped in
  AppendEntries heartbeats,
- or an operator-assigned static weight per member.

The predicate is pluggable (`norn_cluster_set_role_eligibility`) so apps can
supply their own policy; the default is `class == SERVER`.

### 4. Join / leave = Raft membership changes, learner-first

Joining and leaving are Raft **configuration changes**. We use **single-server
changes** (add/remove one member at a time — Ongaro §4.1), which are simpler
and safe. The standard flow, which is exactly what this design wants:

1. A new node joins as a **learner** and catches up the log (no quorum impact
   while it is behind — this is why etcd adds members as learners first).
2. If it is a server, the leader **promotes** it learner → voter once caught
   up (`norn_cluster_promote`).
3. Phones/laptops just **stay learners**.
4. Leaving = demote-to-learner then remove; a server about to leave first
   transfers leadership away if it holds it.

This means **two nodes can bootstrap a cluster** (a 1- or 2-voter seed), and
any number of heterogeneous nodes can then join as learners, with servers
promoted to grow the voting quorum.

## Architecture over libnorn

Layered like the rest of norn: single-threaded, event-loop-driven via
`norn_tick`, `int`/pointer returns, NULL-safe, bounded memory, and a pure core
that is 100% unit-testable without a network.

```
            ┌───────────────────────────────────────────────┐
            │            application (regin, thunder…)       │
            └───────────────────────┬───────────────────────┘
                                    │ norn_cluster.h  (KV + membership API)
            ┌───────────────────────▼───────────────────────┐
 FEAT-026   │  norn_kvstore.c  — replicated state machine    │
            │  (apply PUT/DEL/CAS, watches, snapshots)       │
            ├────────────────────────────────────────────────┤
 FEAT-025   │  norn_cluster.c  — glue: members↔sessions,      │
            │  RPC (de)serialization, timers, membership cfg │
            ├────────────────────────────────────────────────┤
 FEAT-024   │  norn_raft.c  — pure Raft core (I/O-free):     │
            │  term/log/role, PreVote, learners, candidacy   │
            │  eligibility hook, commit rules, snapshots     │
            └───────────────────────┬───────────────────────┘
                                    │ send(peer_pubkey, bytes) / apply(entry)
            ┌───────────────────────▼───────────────────────┐
            │  existing norn: dial/listen/stream (FEAT-016), │
            │  NAT traversal (FEAT-017), crypto, DHT          │
            └────────────────────────────────────────────────┘
```

- **`norn_raft.c/h` (FEAT-024)** — the consensus state machine, completely
  transport- and clock-agnostic. It is fed messages and `tick(now_ms)` and
  emits "send this RPC to peer P" / "apply this committed entry" via a vtable.
  This isolation is what makes 100% branch coverage achievable: elections,
  split votes, log conflicts, snapshots, learner promotion and the candidacy
  predicate are all exercised deterministically in-memory.
- **`norn_cluster.c/h` (FEAT-025)** — maps each member pubkey to a norn
  session/stream (reusing `norn_dial_async`/`norn_listen_async`), bencodes the
  Raft RPCs (RequestVote/PreVote, AppendEntries, InstallSnapshot, TimeoutNow),
  drives the election/heartbeat timers off `norn_tick`, and owns the
  membership configuration and the node-class/eligibility policy.
- **`norn_kvstore.c/h` (FEAT-026)** — the deterministic replicated map.
  Applies committed entries, supports linearizable reads on the leader via
  **ReadIndex**, bounded-stale local reads on learners, prefix **watches**,
  compare-and-swap, and **snapshots** for log compaction.

## Public API sketch (`norn_cluster.h`)

```c
typedef enum {
    NORN_NODE_MOBILE, NORN_NODE_LAPTOP, NORN_NODE_WORKSTATION, NORN_NODE_SERVER
} norn_node_class_t;

typedef struct {
    norn_node_class_t self_class;   /* drives default voter/learner role     */
    uint32_t uptime_score;          /* app-supplied "proven uptime" signal   */
    int election_eligible;          /* -1 = derive from class, 0/1 = force   */
    /* election/heartbeat timeouts, snapshot threshold, byte budgets …       */
} norn_cluster_config_t;

norn_cluster_t *norn_cluster_new(norn_client_t *c, const norn_cluster_config_t *cfg);
void            norn_cluster_free(norn_cluster_t *cl);

/* membership */
int  norn_cluster_bootstrap(norn_cluster_t *cl,                 /* form a new cluster */
                            const unsigned char *peer_pubkeys, int n);
int  norn_cluster_join (norn_cluster_t *cl, const unsigned char *seed_pubkey); /* as learner */
int  norn_cluster_leave(norn_cluster_t *cl);
int  norn_cluster_promote(norn_cluster_t *cl, const unsigned char *member);    /* learner→voter */
int  norn_cluster_demote (norn_cluster_t *cl, const unsigned char *member);    /* voter→learner */

/* replicated KV */
int  norn_cluster_kv_put(norn_cluster_t *cl, const unsigned char *k, size_t kl,
                                             const unsigned char *v, size_t vl);
int  norn_cluster_kv_cas(norn_cluster_t *cl, const unsigned char *k, size_t kl,
                         const unsigned char *expect, size_t el,
                         const unsigned char *v, size_t vl);
int  norn_cluster_kv_del(norn_cluster_t *cl, const unsigned char *k, size_t kl);
int  norn_cluster_kv_get(norn_cluster_t *cl, const unsigned char *k, size_t kl,
                         norn_kv_get_cb cb, void *ud);   /* linearizable or local */
int  norn_cluster_kv_watch(norn_cluster_t *cl, const unsigned char *prefix, size_t pl,
                           norn_kv_event_cb cb, void *ud);

/* introspection / policy */
const unsigned char *norn_cluster_leader(const norn_cluster_t *cl);  /* NULL if unknown */
int  norn_cluster_members(const norn_cluster_t *cl, norn_member_info_t *out, int cap);
void norn_cluster_set_role_eligibility(norn_cluster_t *cl,
                                       int (*eligible)(const norn_member_info_t *, void *), void *ud);
```

Writes issued on a learner/follower are transparently **forwarded to the
leader** over the same norn stream; the call returns once the entry commits (or
via callback in the async variant), keeping the event-loop model intact.

## What stays OUT (consistent with norn's mission)

Per `MILESTONE-0.3.0`, norn surfaces only the *verified peer pubkey* and opaque
payloads. The cluster layer therefore does **not** embed ACLs, accounts, trust
scoring or service authorization — who is *allowed* to join a cluster or write
a key is the application's concern, enforced above norn (e.g. by which pubkeys
the app bootstraps/admits). norn provides the consensus and replication
mechanics; the app provides the policy.

## References

- D. Ongaro, *Consensus: Bridging Theory and Practice* (Raft thesis) — §3.10
  leadership transfer, §4 membership changes, §6 client interaction
  (ReadIndex), §9.6 PreVote.
- etcd raft: learners / non-voting members, learner-first joins, leader
  transfer.
- BEP / norn internals: FEAT-016 (dial→session), FEAT-017 (NAT traversal).
