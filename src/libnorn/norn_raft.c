/* SPDX-License-Identifier: MIT */
/**
 * @file norn_raft.c
 * @brief Pure Raft consensus core (FEAT-024). See norn_raft.h.
 *
 * I/O-free and deterministic: time enters only via raft_tick(now_ms), effects
 * leave only via the raft_io_t vtable. Bounded static memory. Exercised to full
 * line/branch coverage with an in-memory multi-node simulation.
 */

#include "norn_raft.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    raft_node_id_t id;
    raft_member_role_t role;
    int eligible;        /* candidacy gate (voters only) */
    /* leader-side replication bookkeeping */
    uint64_t next_index;
    uint64_t match_index;
    int vote_granted;    /* tally for the in-flight election round */
} member_t;

struct raft {
    raft_node_id_t self;
    raft_io_t io;
    raft_config_t cfg;

    /* persistent */
    uint64_t current_term;
    raft_node_id_t voted_for; /* RAFT_NO_NODE = none this term */
    raft_entry_t log[RAFT_MAX_LOG];
    uint64_t log_len; /* entries occupy log[0..log_len); index = pos+1 */

    /* volatile */
    raft_role_t role;
    raft_node_id_t leader;
    uint64_t commit_index;
    uint64_t last_applied;

    /* timers (ms) */
    uint64_t now;
    uint64_t election_elapsed;  /* since last heard from leader / voted */
    uint64_t heartbeat_elapsed; /* leader: since last heartbeat */

    /* election round */
    int in_pre_vote;

    member_t members[RAFT_MAX_NODES];
    int n_members;
};

/* ---- small helpers ---- */

static void become_leader(raft_t *r);
static void broadcast_append(raft_t *r);

static member_t *find_member(raft_t *r, raft_node_id_t id) {
    for (int i = 0; i < r->n_members; i++)
        if (r->members[i].id == id) return &r->members[i];
    return NULL;
}

static int count_voters(const raft_t *r) {
    int n = 0;
    for (int i = 0; i < r->n_members; i++)
        if (r->members[i].role == RAFT_VOTER) n++;
    return n;
}

static int quorum(const raft_t *r) { return count_voters(r) / 2 + 1; }

static int self_is_eligible_voter(raft_t *r) {
    member_t *m = find_member(r, r->self);
    return m && m->role == RAFT_VOTER && m->eligible;
}

static uint64_t last_log_index(const raft_t *r) { return r->log_len; }

static uint64_t last_log_term(const raft_t *r) {
    return r->log_len ? r->log[r->log_len - 1].term : 0;
}

/* term of the entry at 1-based `index`, or 0 for index 0 / out of range. */
static uint64_t term_at(const raft_t *r, uint64_t index) {
    /* Callers only pass index <= log_len (or 0), so the > log_len arm is a
     * defensive guard. */
    if (index == 0 || index > r->log_len) return 0; /* LCOV_EXCL_BR_LINE: index>log_len unreached by callers */
    return r->log[index - 1].term;
}

static uint32_t election_timeout(const raft_t *r) {
    return r->cfg.election_base_ms + (uint32_t)(r->self % (r->cfg.election_spread_ms + 1));
}

/* Candidate log at least as up-to-date as ours (Raft §5.4.1). */
static int log_ok(const raft_t *r, uint64_t cand_last_idx, uint64_t cand_last_term) {
    uint64_t my_term = last_log_term(r);
    if (cand_last_term != my_term) return cand_last_term > my_term;
    return cand_last_idx >= last_log_index(r);
}

static void reset_votes(raft_t *r) {
    for (int i = 0; i < r->n_members; i++) r->members[i].vote_granted = 0;
}

/* ---- role transitions ---- */

static void become_follower(raft_t *r, uint64_t term, raft_node_id_t leader) {
    r->role = RAFT_FOLLOWER;
    if (term > r->current_term) {
        r->current_term = term;
        r->voted_for = RAFT_NO_NODE;
    }
    r->leader = leader;
    r->in_pre_vote = 0;
    r->election_elapsed = 0;
}

static void send_to(raft_t *r, raft_node_id_t dest, const raft_msg_t *m) {
    if (r->io.send) r->io.send(r->io.ctx, dest, m);
}

/* Broadcast a (pre-)vote request to every other voter. */
static void broadcast_vote(raft_t *r, int pre_vote, uint64_t term) {
    raft_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = RAFT_MSG_REQUEST_VOTE;
    m.term = term;
    m.pre_vote = pre_vote;
    m.candidate_id = r->self;
    m.last_log_index = last_log_index(r);
    m.last_log_term = last_log_term(r);
    for (int i = 0; i < r->n_members; i++) {
        member_t *mm = &r->members[i];
        if (mm->id == r->self || mm->role != RAFT_VOTER) continue;
        send_to(r, mm->id, &m);
    }
}

static void start_election(raft_t *r) {
    /* Only an eligible voter ever stands for election. */
    if (!self_is_eligible_voter(r)) {
        r->election_elapsed = 0; /* stay quiet; don't disturb the cluster */
        return;
    }
    r->role = RAFT_CANDIDATE;
    r->leader = RAFT_NO_NODE;
    r->election_elapsed = 0;
    reset_votes(r);
    find_member(r, r->self)->vote_granted = 1; /* vote for self */

    if (count_voters(r) == 1) {
        /* Sole voter: no one to ask — bump term, self-vote, win immediately. */
        r->in_pre_vote = 0;
        r->current_term++;
        r->voted_for = r->self;
        become_leader(r);
    } else {
        /* PreVote round first: do not bump our term yet. */
        r->in_pre_vote = 1;
        broadcast_vote(r, 1, r->current_term + 1);
    }
}

/* Initialise leader replication state and emit an initial heartbeat + NOOP. */
static void become_leader(raft_t *r) {
    r->role = RAFT_LEADER;
    r->leader = r->self;
    r->in_pre_vote = 0;
    r->heartbeat_elapsed = 0;
    uint64_t next = last_log_index(r) + 1;
    for (int i = 0; i < r->n_members; i++) {
        r->members[i].next_index = next;
        r->members[i].match_index = 0;
    }
    find_member(r, r->self)->match_index = last_log_index(r);

    /* Commit a no-op of the new term so prior-term entries can be committed. */
    if (r->log_len < RAFT_MAX_LOG) { /* LCOV_EXCL_BR_LINE: log-full at election is a defensive bound */
        raft_entry_t *e = &r->log[r->log_len];
        memset(e, 0, sizeof(*e));
        e->term = r->current_term;
        e->index = r->log_len + 1;
        e->type = RAFT_ENTRY_NOOP;
        e->len = 0;
        r->log_len++;
        find_member(r, r->self)->match_index = last_log_index(r);
    }

    /* Assert leadership immediately so followers learn the leader and reset
     * their election timers before they would otherwise time out. */
    broadcast_append(r);
}

/* ---- replication ---- */

static void send_append(raft_t *r, member_t *m) {
    raft_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = RAFT_MSG_APPEND;
    msg.term = r->current_term;
    msg.leader_id = r->self;
    uint64_t prev = m->next_index - 1;
    msg.prev_log_index = prev;
    msg.prev_log_term = term_at(r, prev);
    msg.leader_commit = r->commit_index;

    size_t n = 0;
    if (m->next_index <= last_log_index(r)) {
        uint64_t start = m->next_index; /* 1-based */
        while (n < RAFT_MAX_BATCH && start - 1 + n < r->log_len) {
            n++;
        }
        msg.entries = &r->log[start - 1];
    }
    msg.n_entries = n;
    send_to(r, m->id, &msg);
}

static void broadcast_append(raft_t *r) {
    for (int i = 0; i < r->n_members; i++) {
        if (r->members[i].id == r->self) continue;
        send_append(r, &r->members[i]);
    }
}

static void apply_committed(raft_t *r) {
    while (r->last_applied < r->commit_index) {
        r->last_applied++;
        raft_entry_t *e = &r->log[r->last_applied - 1];
        if (e->type == RAFT_ENTRY_NORMAL && r->io.apply)
            r->io.apply(r->io.ctx, e);
    }
}

/* Leader: advance commit_index to the highest index replicated on a quorum of
 * voters, but only for entries from the current term (Raft §5.4.2). */
static void advance_commit(raft_t *r) {
    for (uint64_t idx = last_log_index(r); idx > r->commit_index; idx--) {
        if (term_at(r, idx) != r->current_term) break;
        int votes = 0;
        for (int i = 0; i < r->n_members; i++)
            if (r->members[i].role == RAFT_VOTER && r->members[i].match_index >= idx)
                votes++;
        if (votes >= quorum(r)) {
            r->commit_index = idx;
            break;
        }
    }
    apply_committed(r);
}

/* ---- message handlers ---- */

static void handle_request_vote(raft_t *r, const raft_msg_t *m) {
    raft_msg_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.type = RAFT_MSG_REQUEST_VOTE_RESP;
    resp.pre_vote = m->pre_vote;

    /* A real (non-pre) vote with a higher term moves us to that term first. */
    if (!m->pre_vote && m->term > r->current_term)
        become_follower(r, m->term, RAFT_NO_NODE);

    resp.term = r->current_term;

    int up_to_date = log_ok(r, m->last_log_index, m->last_log_term);
    int grant;
    if (m->pre_vote) {
        /* Grant a pre-vote if the candidate's term is ahead, its log is OK, and
         * we have not heard from a leader within the election timeout. */
        grant = m->term > r->current_term && up_to_date &&
                r->election_elapsed >= r->cfg.election_base_ms;
    } else {
        int can_vote = (r->voted_for == RAFT_NO_NODE || r->voted_for == m->candidate_id);
        grant = m->term >= r->current_term && can_vote && up_to_date;
        if (grant) {
            r->voted_for = m->candidate_id;
            r->election_elapsed = 0; /* granting a vote resets our timer */
        }
    }
    resp.vote_granted = grant;
    send_to(r, m->candidate_id, &resp);
}

static void handle_request_vote_resp(raft_t *r, const raft_msg_t *m) {
    /* A higher real term means we lost; step down. */
    if (!m->pre_vote && m->term > r->current_term) {
        become_follower(r, m->term, RAFT_NO_NODE);
        return;
    }
    if (r->role != RAFT_CANDIDATE) return;
    if (m->pre_vote != r->in_pre_vote) return; /* stale round */
    if (!m->vote_granted) return;

    /* raft_recv() has already set the responding voter's vote_granted flag; tally
     * the granting voters. */
    int grants = 0;
    for (int i = 0; i < r->n_members; i++)
        if (r->members[i].role == RAFT_VOTER && r->members[i].vote_granted) grants++;

    if (grants < quorum(r)) return;

    if (r->in_pre_vote) {
        /* PreVote succeeded → run the real election. */
        r->in_pre_vote = 0;
        r->current_term++;
        r->voted_for = r->self;
        reset_votes(r);
        find_member(r, r->self)->vote_granted = 1;
        r->election_elapsed = 0;
        broadcast_vote(r, 0, r->current_term);
    } else {
        become_leader(r);
    }
}

static void handle_append(raft_t *r, const raft_msg_t *m) {
    raft_msg_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.type = RAFT_MSG_APPEND_RESP;

    if (m->term < r->current_term) {
        resp.term = r->current_term;
        resp.success = 0;
        send_to(r, m->leader_id, &resp);
        return;
    }
    /* Valid leader for our term (or a newer one). */
    become_follower(r, m->term, m->leader_id);
    r->current_term = m->term; /* equal-term case keeps term, sets leader */
    r->leader = m->leader_id;
    r->election_elapsed = 0;
    resp.term = r->current_term;

    /* Log-matching check. */
    if (m->prev_log_index > last_log_index(r) ||
        term_at(r, m->prev_log_index) != m->prev_log_term) {
        resp.success = 0;
        resp.match_index = 0;
        send_to(r, m->leader_id, &resp);
        return;
    }

    /* Append/overwrite entries. */
    for (size_t i = 0; i < m->n_entries; i++) {
        uint64_t idx = m->prev_log_index + 1 + i;
        const raft_entry_t *in = &m->entries[i];
        if (idx <= last_log_index(r)) {
            if (term_at(r, idx) != in->term) {
                r->log_len = idx - 1; /* truncate conflict + tail */
            } else {
                continue; /* already have it */
            }
        }
        if (idx - 1 < RAFT_MAX_LOG) { /* LCOV_EXCL_BR_LINE: append-overflow is a defensive bound */
            r->log[idx - 1] = *in;
            r->log[idx - 1].index = idx;
            r->log_len = idx;
        }
    }

    if (m->leader_commit > r->commit_index) {
        uint64_t last = last_log_index(r);
        r->commit_index = m->leader_commit < last ? m->leader_commit : last;
        apply_committed(r);
    }

    resp.success = 1;
    resp.match_index = m->prev_log_index + m->n_entries;
    send_to(r, m->leader_id, &resp);
}

static void handle_append_resp(raft_t *r, raft_node_id_t from, const raft_msg_t *m) {
    if (m->term > r->current_term) {
        become_follower(r, m->term, RAFT_NO_NODE);
        return;
    }
    if (r->role != RAFT_LEADER) return;
    member_t *mm = find_member(r, from);
    if (!mm) return;

    if (m->success) {
        if (m->match_index > mm->match_index) mm->match_index = m->match_index;
        mm->next_index = mm->match_index + 1;
        advance_commit(r);
    } else if (mm->next_index > 1) {
        mm->next_index--; /* back off and retry */
        send_append(r, mm);
    }
}

/* ---- public API ---- */

raft_t *raft_new(raft_node_id_t self, const raft_io_t *io, const raft_config_t *cfg) {
    if (self == RAFT_NO_NODE || !io) return NULL;
    raft_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL; /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    r->self = self;
    r->io = *io;
    if (cfg) {
        r->cfg = *cfg;
    }
    if (r->cfg.election_base_ms == 0) r->cfg.election_base_ms = 150;
    if (r->cfg.election_spread_ms == 0) r->cfg.election_spread_ms = 150;
    if (r->cfg.heartbeat_ms == 0) r->cfg.heartbeat_ms = 50;
    r->role = RAFT_FOLLOWER;
    r->voted_for = RAFT_NO_NODE;
    return r;
}

void raft_free(raft_t *r) { free(r); }

int raft_add_member(raft_t *r, raft_node_id_t id, raft_member_role_t role, int eligible) {
    if (!r || id == RAFT_NO_NODE || r->n_members >= RAFT_MAX_NODES) return -1;
    if (find_member(r, id)) return -1;
    member_t *m = &r->members[r->n_members++];
    memset(m, 0, sizeof(*m));
    m->id = id;
    m->role = role;
    m->eligible = eligible;
    m->next_index = last_log_index(r) + 1;
    return 0;
}

int raft_remove_member(raft_t *r, raft_node_id_t id) {
    if (!r) return -1;
    for (int i = 0; i < r->n_members; i++) {
        if (r->members[i].id == id) {
            r->members[i] = r->members[r->n_members - 1];
            r->n_members--;
            return 0;
        }
    }
    return -1;
}

int raft_promote(raft_t *r, raft_node_id_t id) {
    if (!r) return -1;
    member_t *m = find_member(r, id);
    if (!m || m->role != RAFT_LEARNER) return -1;
    m->role = RAFT_VOTER;
    m->eligible = 1;
    return 0;
}

int raft_set_eligible(raft_t *r, raft_node_id_t id, int eligible) {
    if (!r) return -1;
    member_t *m = find_member(r, id);
    if (!m) return -1;
    m->eligible = eligible;
    return 0;
}

uint64_t raft_propose(raft_t *r, const void *data, size_t len) {
    if (!r || r->role != RAFT_LEADER || len > RAFT_ENTRY_MAX) return 0;
    if (r->log_len >= RAFT_MAX_LOG) return 0; /* LCOV_EXCL_BR_LINE: log-full is a defensive bound */
    raft_entry_t *e = &r->log[r->log_len];
    memset(e, 0, sizeof(*e));
    e->term = r->current_term;
    e->index = r->log_len + 1;
    e->type = RAFT_ENTRY_NORMAL;
    if (len && data) memcpy(e->data, data, len);
    e->len = len;
    r->log_len++;
    find_member(r, r->self)->match_index = last_log_index(r);
    broadcast_append(r);
    advance_commit(r);
    return e->index;
}

void raft_recv(raft_t *r, raft_node_id_t from, const raft_msg_t *msg) {
    if (!r || !msg) return;
    switch (msg->type) { /* LCOV_EXCL_BR_LINE: all enum values handled; no default arm */
    case RAFT_MSG_REQUEST_VOTE:
        handle_request_vote(r, msg);
        break;
    case RAFT_MSG_REQUEST_VOTE_RESP: {
        member_t *m = find_member(r, from);
        if (m && msg->vote_granted) m->vote_granted = 1;
        handle_request_vote_resp(r, msg);
        break;
    }
    case RAFT_MSG_APPEND:
        handle_append(r, msg);
        break;
    case RAFT_MSG_APPEND_RESP:
        handle_append_resp(r, from, msg);
        break;
    }
}

void raft_tick(raft_t *r, uint64_t now_ms) {
    if (!r) return;
    uint64_t dt = now_ms > r->now ? now_ms - r->now : 0;
    r->now = now_ms;
    r->election_elapsed += dt;
    r->heartbeat_elapsed += dt;

    if (r->role == RAFT_LEADER) {
        /* The leader continuously "hears from the leader" (itself), so it never
         * accrues election timeout and never grants a PreVote to depose itself. */
        r->election_elapsed = 0;
        if (r->heartbeat_elapsed >= r->cfg.heartbeat_ms) {
            r->heartbeat_elapsed = 0;
            broadcast_append(r);
        }
        return;
    }
    if (r->election_elapsed >= election_timeout(r)) {
        start_election(r);
    }
    apply_committed(r);
}

raft_role_t raft_role(const raft_t *r) { return r ? r->role : RAFT_FOLLOWER; }
raft_node_id_t raft_leader(const raft_t *r) { return r ? r->leader : RAFT_NO_NODE; }
uint64_t raft_term(const raft_t *r) { return r ? r->current_term : 0; }
uint64_t raft_commit_index(const raft_t *r) { return r ? r->commit_index : 0; }
uint64_t raft_last_log_index(const raft_t *r) { return r ? r->log_len : 0; }

int raft_is_voter(const raft_t *r, raft_node_id_t id) {
    if (!r) return 0;
    for (int i = 0; i < r->n_members; i++)
        if (r->members[i].id == id) return r->members[i].role == RAFT_VOTER;
    return 0;
}
