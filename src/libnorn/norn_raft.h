/**
 * @file norn_raft.h
 * @brief Pure Raft consensus core (FEAT-024) — class-aware, I/O-free.
 *
 * A transport- and clock-agnostic Raft state machine: it is fed inbound
 * messages ([`raft_recv`]) and a monotonic tick ([`raft_tick`]), and emits its
 * effects ("send this RPC to peer P", "apply this committed entry") through a
 * callback vtable ([`raft_io_t`]). No sockets, no clock, no globals — so the
 * whole algorithm (elections, log replication, PreVote, learners, candidacy
 * eligibility, membership changes) is unit-testable in-memory and
 * deterministically.
 *
 * @section heterogeneity Class-aware membership (the norn twist)
 * Two liveness-only, safety-preserving features let a cluster of mixed
 * availability (phones, laptops, servers) work:
 *   - **Learners** (`RAFT_LEARNER`) receive the replicated log but are excluded
 *     from quorum and never vote or lead. Quorum is computed over **voters**
 *     only, so a majority of always-on servers commits writes regardless of how
 *     many edge members are offline.
 *   - **Candidacy eligibility**: a voter only stands for election if its
 *     `eligible` flag is set (e.g. "is a proven-uptime server"), plus PreVote
 *     to stop flaky members disrupting a healthy leader. This restricts *who
 *     leads*, never *who votes* or quorum math, so Raft safety is unaffected.
 *
 * @section determinism Determinism
 * All timing flows through `raft_tick(now_ms)`. Election timeouts are derived
 * deterministically from the node id (base + id-spread), so a multi-node
 * simulation is reproducible.
 */

#ifndef NORN_RAFT_H
#define NORN_RAFT_H

#include <stddef.h>
#include <stdint.h>

/** Max members (voters + learners) in a cluster. */
#define RAFT_MAX_NODES 16
/** Max in-memory log entries (bounded; compaction is a later increment). */
#define RAFT_MAX_LOG 512
/** Max bytes of application data per log entry. */
#define RAFT_ENTRY_MAX 256
/** Max entries shipped in one AppendEntries. */
#define RAFT_MAX_BATCH 8
/** Reserved "no node" id. Real members use id >= 1. */
#define RAFT_NO_NODE 0

typedef uint64_t raft_node_id_t;

/** Role of this node in the current term. */
typedef enum { RAFT_FOLLOWER, RAFT_CANDIDATE, RAFT_LEADER } raft_role_t;

/** Membership class: counts toward quorum (voter) or not (learner). */
typedef enum { RAFT_VOTER, RAFT_LEARNER } raft_member_role_t;

/** Log entry kind. */
typedef enum {
    RAFT_ENTRY_NORMAL = 0, /**< application command */
    RAFT_ENTRY_NOOP,       /**< leader's no-op committed at term start */
} raft_entry_type_t;

/** A replicated log entry. */
typedef struct {
    uint64_t term;
    uint64_t index; /**< 1-based */
    raft_entry_type_t type;
    unsigned char data[RAFT_ENTRY_MAX];
    size_t len;
} raft_entry_t;

/** RPC message kinds. */
typedef enum {
    RAFT_MSG_REQUEST_VOTE,
    RAFT_MSG_REQUEST_VOTE_RESP,
    RAFT_MSG_APPEND,
    RAFT_MSG_APPEND_RESP,
} raft_msg_type_t;

/**
 * A flat RPC message. Which fields are meaningful depends on `type`.
 * For RAFT_MSG_APPEND, `entries` borrows the sender's log for the duration of
 * the `send`/`recv` call only — the receiver copies what it keeps.
 */
typedef struct {
    raft_msg_type_t type;
    uint64_t term;

    /* RequestVote (and its PreVote variant) */
    int pre_vote;
    raft_node_id_t candidate_id;
    uint64_t last_log_index;
    uint64_t last_log_term;

    /* RequestVoteResp */
    int vote_granted;

    /* AppendEntries */
    raft_node_id_t leader_id;
    uint64_t prev_log_index;
    uint64_t prev_log_term;
    const raft_entry_t *entries;
    size_t n_entries;
    uint64_t leader_commit;

    /* AppendEntriesResp */
    int success;
    uint64_t match_index;
} raft_msg_t;

/** Effects vtable: how the core talks to the world. */
typedef struct {
    /** Send `msg` to member `dest`. */
    void (*send)(void *ctx, raft_node_id_t dest, const raft_msg_t *msg);
    /** Apply a committed entry to the application state machine. */
    void (*apply)(void *ctx, const raft_entry_t *entry);
    void *ctx;
} raft_io_t;

/** Tunables. */
typedef struct {
    uint32_t election_base_ms;   /**< min election timeout (default 150) */
    uint32_t election_spread_ms; /**< id-derived jitter range (default 150) */
    uint32_t heartbeat_ms;       /**< leader heartbeat interval (default 50) */
} raft_config_t;

typedef struct raft raft_t;

/* === Lifecycle === */

/**
 * Create a Raft node. `self` must be >= 1. `io` is copied. `cfg` may be NULL
 * for defaults. The node starts as a follower with an empty configuration; add
 * members (including itself) with [`raft_add_member`].
 */
raft_t *raft_new(raft_node_id_t self, const raft_io_t *io, const raft_config_t *cfg);
void raft_free(raft_t *r);

/* === Membership === */

/** Add/declare a member with the given class and candidacy eligibility.
 *  Returns 0 on success, -1 on error (full, dup, or bad id). */
int raft_add_member(raft_t *r, raft_node_id_t id, raft_member_role_t role, int eligible);
/** Remove a member. Returns 0 on success, -1 if absent. */
int raft_remove_member(raft_t *r, raft_node_id_t id);
/** Promote a learner to voter. Returns 0 on success, -1 if absent/not a learner. */
int raft_promote(raft_t *r, raft_node_id_t id);
/** Set a voter's candidacy eligibility (the "proven-uptime server" gate). */
int raft_set_eligible(raft_t *r, raft_node_id_t id, int eligible);

/* === Driving === */

/** Propose an application command (leader only). Returns the assigned log index,
 *  or 0 on error (not leader, too big, log full). */
uint64_t raft_propose(raft_t *r, const void *data, size_t len);
/** Feed an inbound message from member `from`. */
void raft_recv(raft_t *r, raft_node_id_t from, const raft_msg_t *msg);
/** Advance time. Drives election/heartbeat timers and applies committed entries. */
void raft_tick(raft_t *r, uint64_t now_ms);

/* === Introspection === */

raft_role_t raft_role(const raft_t *r);
raft_node_id_t raft_leader(const raft_t *r);
uint64_t raft_term(const raft_t *r);
uint64_t raft_commit_index(const raft_t *r);
uint64_t raft_last_log_index(const raft_t *r);
int raft_is_voter(const raft_t *r, raft_node_id_t id);

#endif /* NORN_RAFT_H */
