#ifndef NORN_RAFT_H
#define NORN_RAFT_H

/* norn_raft — a pure, transport- and clock-agnostic Raft consensus core
 * (FEAT-024, MILESTONE-0.11.0). It is fed inbound messages and a monotonic
 * tick(now_ms), and emits actions ("send RPC to peer", "apply committed entry",
 * "persist term/vote") through a callback vtable. No sockets, no clock, no
 * allocation in the step path — the log is a bounded in-struct array.
 *
 * Heterogeneous membership (servers lead, phones are learners) is expressed by
 * two liveness-only, safety-preserving features that live entirely here:
 *   - voter vs. learner members (quorum counts voters only; learners replicate
 *     but never vote or lead), and
 *   - a candidacy-eligibility predicate (a follower becomes a candidate only if
 *     it is a voter AND the predicate allows it) plus PreVote.
 * See docs/cluster-kv.md. All public functions are NULL-safe; ints return 0 on
 * success and -1 on error unless noted.
 */

#include <stddef.h>
#include <stdint.h>

#define NORN_RAFT_MAX_PEERS 16  /* including self */
#define NORN_RAFT_MAX_LOG 512   /* in-memory log window between snapshots */
#define NORN_RAFT_MAX_ENTRY 256 /* bytes of opaque payload per data entry */

typedef enum {
    NORN_RAFT_FOLLOWER,
    NORN_RAFT_CANDIDATE,
    NORN_RAFT_LEADER,
    NORN_RAFT_LEARNER
} norn_raft_role_t;

/* Log entry kinds: opaque data, a no-op (leader's term marker), or a
 * single-server membership change (Ongaro §4.1) that takes effect when appended. */
typedef enum {
    NORN_RAFT_ENTRY_DATA,
    NORN_RAFT_ENTRY_NOOP,
    NORN_RAFT_ENTRY_ADD,     /* add a member (cfg_voter chooses voter/learner) */
    NORN_RAFT_ENTRY_REMOVE,  /* remove a member */
    NORN_RAFT_ENTRY_PROMOTE  /* learner -> voter */
} norn_raft_entry_type_t;

typedef struct {
    uint32_t term;
    uint32_t index;
    uint8_t type; /* norn_raft_entry_type_t */
    uint32_t cfg_node;  /* config entries: affected member id */
    uint8_t cfg_voter;  /* ADD: 1 = voter, 0 = learner */
    uint16_t data_len;
    uint8_t data[NORN_RAFT_MAX_ENTRY];
} norn_raft_entry_t;

typedef enum {
    NORN_RAFT_MSG_PREVOTE_REQ,
    NORN_RAFT_MSG_PREVOTE_RESP,
    NORN_RAFT_MSG_VOTE_REQ,
    NORN_RAFT_MSG_VOTE_RESP,
    NORN_RAFT_MSG_APPEND_REQ,
    NORN_RAFT_MSG_APPEND_RESP,
    NORN_RAFT_MSG_TIMEOUTNOW
} norn_raft_msg_type_t;

/* One wire message. AppendEntries carries at most one entry (has_entry); this
 * keeps the message fixed-size and the core fully testable — nextIndex regression
 * still streams a follower up to date one entry per round. */
typedef struct {
    uint8_t type; /* norn_raft_msg_type_t */
    uint32_t term;
    uint32_t from;
    uint32_t to;
    /* (pre)vote */
    uint32_t last_log_index;
    uint32_t last_log_term;
    uint8_t vote_granted; /* *_RESP */
    /* append */
    uint32_t prev_log_index;
    uint32_t prev_log_term;
    uint32_t leader_commit;
    uint8_t has_entry;
    norn_raft_entry_t entry;
    /* append resp */
    uint8_t success;
    uint32_t match_index; /* follower's last index on success (for nextIndex) */
} norn_raft_msg_t;

typedef struct {
    /* Emit an outbound RPC. */
    void (*send)(void *ud, const norn_raft_msg_t *msg);
    /* A committed entry is ready to apply to the state machine. */
    void (*apply)(void *ud, const norn_raft_entry_t *entry);
    /* Persist (currentTerm, votedFor) before responding to votes (durability). */
    void (*save_state)(void *ud, uint32_t term, int32_t voted_for);
    /* Candidacy eligibility: return non-zero if this node may stand for election
     * now (e.g. proven-uptime server). NULL => always eligible (still gated on
     * being a voter). */
    int (*eligible)(void *ud);
    void *ud;
} norn_raft_callbacks_t;

typedef struct {
    uint32_t id;
    uint8_t voter;    /* counts toward quorum + may be granted votes */
    uint8_t active;   /* slot in use */
    uint32_t next_index;
    uint32_t match_index;
    uint8_t vote_granted; /* tally during (pre)vote */
} norn_raft_peer_t;

typedef struct {
    uint32_t self_id;
    uint8_t self_voter;
    norn_raft_role_t role;

    uint32_t current_term;
    int32_t voted_for; /* -1 none */

    norn_raft_entry_t log[NORN_RAFT_MAX_LOG];
    uint32_t log_len;     /* entries currently held */
    uint32_t base_index;  /* lastIncludedIndex (snapshot boundary); log[0].index = base_index+1 */
    uint32_t base_term;   /* lastIncludedTerm */

    uint32_t commit_index;
    uint32_t last_applied;

    norn_raft_peer_t peers[NORN_RAFT_MAX_PEERS];
    uint32_t peer_count;

    uint32_t leader_id; /* 0 = unknown */

    /* timers (all in caller-supplied ms via tick) */
    uint64_t now;
    uint64_t election_deadline;
    uint64_t heartbeat_deadline;
    uint64_t last_leader_contact; /* when we last heard a valid leader (PreVote gate) */
    uint32_t election_min, election_max, heartbeat_interval;
    uint32_t rng; /* deterministic jitter */

    /* tally for the in-flight (pre)vote round */
    uint32_t prevotes;
    uint32_t votes;
    uint8_t in_prevote;

    norn_raft_callbacks_t cb;
} norn_raft_t;

/* Initialise a core. self_voter: is this node a voter (server) or a learner?
 * election_min/max bound the randomized election timeout; heartbeat_interval is
 * the leader's append cadence; seed makes jitter deterministic. */
int norn_raft_init(norn_raft_t *r, uint32_t self_id, int self_voter,
                   const norn_raft_callbacks_t *cb, uint32_t election_min,
                   uint32_t election_max, uint32_t heartbeat_interval,
                   uint32_t seed);

/* Add a member to the *initial* configuration (before the cluster starts). For
 * runtime changes use norn_raft_propose_config on the leader. Returns 0 / -1. */
int norn_raft_add_peer(norn_raft_t *r, uint32_t id, int voter);

/* Advance the clock; may start an election or send heartbeats. */
void norn_raft_tick(norn_raft_t *r, uint64_t now_ms);

/* Process one inbound message. */
void norn_raft_step(norn_raft_t *r, const norn_raft_msg_t *msg);

/* Leader-only: append an opaque data entry. Returns its log index, or -1. */
int64_t norn_raft_propose(norn_raft_t *r, const void *data, size_t len);

/* Leader-only: append a single-server membership change. Returns index or -1. */
int64_t norn_raft_propose_config(norn_raft_t *r, norn_raft_entry_type_t type,
                                 uint32_t node, int voter);

/* Compact the log up to and including `index` (must be <= commit_index): drop
 * applied entries, recording (index, its term) as the new base. Returns 0/-1. */
int norn_raft_snapshot(norn_raft_t *r, uint32_t index);

/* Introspection. */
norn_raft_role_t norn_raft_role(const norn_raft_t *r);
uint32_t norn_raft_term(const norn_raft_t *r);
uint32_t norn_raft_commit_index(const norn_raft_t *r);
uint32_t norn_raft_last_index(const norn_raft_t *r);
uint32_t norn_raft_leader(const norn_raft_t *r);
int norn_raft_is_voter(const norn_raft_t *r);
uint32_t norn_raft_peer_count(const norn_raft_t *r);

#endif /* NORN_RAFT_H */
