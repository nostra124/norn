/* norn_raft — pure Raft consensus core (FEAT-024). See norn_raft.h. */

#include "norn_raft.h"

#include <string.h>

/* ── deterministic RNG (xorshift32) for election-timeout jitter ── */
static uint32_t rng_next(norn_raft_t *r)
{
    uint32_t x = r->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0) { /* LCOV_EXCL_BR_LINE: xorshift32 from a non-zero seed never yields 0 */
        x = 0x1234567u; /* LCOV_EXCL_LINE */
    }
    r->rng = x;
    return x;
}

/* ── log geometry helpers ── */
static uint32_t last_index(const norn_raft_t *r)
{
    return r->base_index + r->log_len;
}

static uint32_t last_term(const norn_raft_t *r)
{
    return r->log_len ? r->log[r->log_len - 1].term : r->base_term;
}

/* term of the entry at absolute `index`, or 0 if not known (out of range). */
static uint32_t term_at(const norn_raft_t *r, uint32_t index)
{
    if (index == r->base_index) {
        return r->base_term;
    }
    if (index > r->base_index && index <= last_index(r)) {
        return r->log[index - r->base_index - 1].term;
    }
    return 0;
}

static norn_raft_entry_t *entry_at(norn_raft_t *r, uint32_t index)
{
    if (index > r->base_index && index <= last_index(r)) {
        return &r->log[index - r->base_index - 1];
    }
    return NULL; /* LCOV_EXCL_LINE: callers only request in-range indices */
}

static norn_raft_peer_t *find_peer(norn_raft_t *r, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < r->peer_count; i++) {
        if (r->peers[i].active && r->peers[i].id == id) {
            return &r->peers[i];
        }
    }
    return NULL;
}

/* number of voters in the configuration, including self if a voter. */
static uint32_t voter_count(const norn_raft_t *r)
{
    uint32_t n = r->self_voter ? 1u : 0u;
    uint32_t i;
    for (i = 0; i < r->peer_count; i++) {
        if (r->peers[i].active && r->peers[i].voter) {
            n++;
        }
    }
    return n;
}

static uint32_t quorum(const norn_raft_t *r)
{
    return voter_count(r) / 2u + 1u;
}

/* candidate's log at least as up-to-date as ours (Raft §5.4.1). */
static int log_up_to_date(const norn_raft_t *r, uint32_t cand_last_term,
                          uint32_t cand_last_index)
{
    uint32_t my_term = last_term(r);
    uint32_t my_index = last_index(r);
    if (cand_last_term != my_term) {
        return cand_last_term > my_term;
    }
    return cand_last_index >= my_index;
}

static void reset_election_timer(norn_raft_t *r)
{
    uint32_t span = r->election_max - r->election_min + 1u;
    r->election_deadline = r->now + r->election_min + (rng_next(r) % span);
}

static void persist(norn_raft_t *r)
{
    if (r->cb.save_state) {
        r->cb.save_state(r->cb.ud, r->current_term, r->voted_for);
    }
}

static void clear_tally(norn_raft_t *r)
{
    uint32_t i;
    for (i = 0; i < r->peer_count; i++) {
        r->peers[i].vote_granted = 0;
    }
    r->prevotes = 0;
    r->votes = 0;
}

/* ── role transitions ── */
static void become_follower(norn_raft_t *r, uint32_t term)
{
    if (term > r->current_term) {
        r->current_term = term;
        r->voted_for = -1;
        persist(r);
    }
    r->role = r->self_voter ? NORN_RAFT_FOLLOWER : NORN_RAFT_LEARNER;
    r->in_prevote = 0;
}

static void send_msg(norn_raft_t *r, norn_raft_msg_t *m)
{
    m->from = r->self_id;
    m->term = r->current_term;
    if (r->cb.send) {
        r->cb.send(r->cb.ud, m);
    }
}

/* leader → one peer: AppendEntries from peer->next_index (one entry or heartbeat). */
static void send_append(norn_raft_t *r, norn_raft_peer_t *p)
{
    norn_raft_msg_t m;
    uint32_t prev;
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_REQ;
    m.to = p->id;
    if (p->next_index <= r->base_index) {
        p->next_index = r->base_index + 1; /* clamp to what we still hold */
    }
    prev = p->next_index - 1;
    m.prev_log_index = prev;
    m.prev_log_term = term_at(r, prev);
    m.leader_commit = r->commit_index;
    if (p->next_index <= last_index(r)) {
        norn_raft_entry_t *e = entry_at(r, p->next_index);
        m.has_entry = 1;
        m.entry = *e;
    }
    send_msg(r, &m);
}

static void broadcast_append(norn_raft_t *r)
{
    uint32_t i;
    for (i = 0; i < r->peer_count; i++) {
        if (r->peers[i].active) {
            send_append(r, &r->peers[i]);
        }
    }
    r->heartbeat_deadline = r->now + r->heartbeat_interval;
}

/* membership change takes effect when the entry is appended (Ongaro §4.1). */
static void apply_config(norn_raft_t *r, const norn_raft_entry_t *e)
{
    norn_raft_peer_t *p;
    switch (e->type) {
    case NORN_RAFT_ENTRY_ADD:
        if (e->cfg_node == r->self_id) {
            r->self_voter = e->cfg_voter ? 1 : 0;
            break;
        }
        p = find_peer(r, e->cfg_node);
        if (p) {
            p->voter = e->cfg_voter ? 1 : 0;
        } else if (r->peer_count < NORN_RAFT_MAX_PEERS) {
            p = &r->peers[r->peer_count++];
            memset(p, 0, sizeof(*p));
            p->id = e->cfg_node;
            p->voter = e->cfg_voter ? 1 : 0;
            p->active = 1;
            p->next_index = last_index(r) + 1;
        }
        break;
    case NORN_RAFT_ENTRY_REMOVE:
        if (e->cfg_node == r->self_id) {
            r->self_voter = 0;
            r->role = NORN_RAFT_LEARNER;
            break;
        }
        p = find_peer(r, e->cfg_node);
        if (p) {
            p->active = 0;
        }
        break;
    case NORN_RAFT_ENTRY_PROMOTE:
        if (e->cfg_node == r->self_id) {
            r->self_voter = 1;
            if (r->role == NORN_RAFT_LEARNER) {
                r->role = NORN_RAFT_FOLLOWER;
            }
            break;
        }
        p = find_peer(r, e->cfg_node);
        if (p) {
            p->voter = 1;
        }
        break;
    default: /* LCOV_EXCL_LINE: apply_config is only called for is_config() types */
        break; /* LCOV_EXCL_LINE */
    }
}

static int is_config(uint8_t type)
{
    return type == NORN_RAFT_ENTRY_ADD || type == NORN_RAFT_ENTRY_REMOVE ||
           type == NORN_RAFT_ENTRY_PROMOTE;
}

/* append one entry to the local log (already term/index-stamped). */
static void log_append(norn_raft_t *r, const norn_raft_entry_t *e)
{
    if (r->log_len >= NORN_RAFT_MAX_LOG) { /* LCOV_EXCL_BR_LINE: bounded-log overflow guard */
        return; /* LCOV_EXCL_LINE: caller snapshots before the window fills */
    }
    r->log[r->log_len++] = *e;
    if (is_config(e->type)) {
        apply_config(r, e);
    }
}

static void apply_committed(norn_raft_t *r)
{
    while (r->last_applied < r->commit_index) {
        norn_raft_entry_t *e;
        r->last_applied++;
        e = entry_at(r, r->last_applied);
        if (e && e->type == NORN_RAFT_ENTRY_DATA && r->cb.apply) {
            r->cb.apply(r->cb.ud, e);
        }
    }
}

/* leader: advance commit to the highest index replicated on a voter majority
 * whose entry is from the current term (Raft §5.4.2). */
static void leader_advance_commit(norn_raft_t *r)
{
    uint32_t n;
    for (n = last_index(r); n > r->commit_index; n--) {
        uint32_t count = (r->self_voter ? 1u : 0u); /* leader holds it */
        uint32_t i;
        if (term_at(r, n) != r->current_term) {
            continue; /* never commit a prior term by counting alone */
        }
        for (i = 0; i < r->peer_count; i++) {
            if (r->peers[i].active && r->peers[i].voter &&
                r->peers[i].match_index >= n) {
                count++;
            }
        }
        if (count >= quorum(r)) {
            r->commit_index = n;
            apply_committed(r);
            return;
        }
    }
}

static void become_leader(norn_raft_t *r)
{
    uint32_t i;
    norn_raft_entry_t noop;
    r->role = NORN_RAFT_LEADER;
    r->leader_id = r->self_id;
    r->last_leader_contact = r->now;
    for (i = 0; i < r->peer_count; i++) {
        r->peers[i].next_index = last_index(r) + 1;
        r->peers[i].match_index = 0;
    }
    /* a no-op for the new term lets prior-term entries commit safely. */
    memset(&noop, 0, sizeof(noop));
    noop.type = NORN_RAFT_ENTRY_NOOP;
    noop.term = r->current_term;
    noop.index = last_index(r) + 1;
    log_append(r, &noop);
    broadcast_append(r);
}

static void become_candidate(norn_raft_t *r)
{
    uint32_t i;
    r->current_term++;
    r->voted_for = (int32_t)r->self_id;
    r->role = NORN_RAFT_CANDIDATE;
    r->in_prevote = 0;
    persist(r);
    clear_tally(r);
    r->votes = 1; /* self */
    reset_election_timer(r);
    for (i = 0; i < r->peer_count; i++) {
        norn_raft_msg_t m;
        if (!r->peers[i].active || !r->peers[i].voter) {
            continue;
        }
        memset(&m, 0, sizeof(m));
        m.type = NORN_RAFT_MSG_VOTE_REQ;
        m.to = r->peers[i].id;
        m.last_log_index = last_index(r);
        m.last_log_term = last_term(r);
        send_msg(r, &m);
    }
    if (r->votes >= quorum(r)) {
        become_leader(r); /* single-voter cluster */
    }
}

static int may_stand(norn_raft_t *r)
{
    if (!r->self_voter) {
        return 0;
    }
    if (r->cb.eligible && !r->cb.eligible(r->cb.ud)) {
        return 0;
    }
    return 1;
}

static void start_prevote(norn_raft_t *r)
{
    uint32_t i;
    if (!may_stand(r)) {
        return;
    }
    r->in_prevote = 1;
    clear_tally(r);
    r->prevotes = 1; /* self */
    reset_election_timer(r);
    for (i = 0; i < r->peer_count; i++) {
        norn_raft_msg_t m;
        if (!r->peers[i].active || !r->peers[i].voter) {
            continue;
        }
        memset(&m, 0, sizeof(m));
        m.type = NORN_RAFT_MSG_PREVOTE_REQ;
        m.to = r->peers[i].id;
        m.term = r->current_term + 1; /* hypothetical term; not adopted */
        m.last_log_index = last_index(r);
        m.last_log_term = last_term(r);
        m.from = r->self_id;
        if (r->cb.send) {
            r->cb.send(r->cb.ud, &m); /* bypass send_msg: keep hypothetical term */
        }
    }
    if (r->prevotes >= quorum(r)) {
        become_candidate(r);
    }
}

/* ── public API ── */
int norn_raft_init(norn_raft_t *r, uint32_t self_id, int self_voter,
                   const norn_raft_callbacks_t *cb, uint32_t election_min,
                   uint32_t election_max, uint32_t heartbeat_interval,
                   uint32_t seed)
{
    if (!r || !cb || election_max < election_min || election_min == 0) {
        return -1;
    }
    memset(r, 0, sizeof(*r));
    r->self_id = self_id;
    r->self_voter = self_voter ? 1 : 0;
    r->role = r->self_voter ? NORN_RAFT_FOLLOWER : NORN_RAFT_LEARNER;
    r->voted_for = -1;
    r->election_min = election_min;
    r->election_max = election_max;
    r->heartbeat_interval = heartbeat_interval;
    r->rng = seed ? seed : 0xABCDEF01u;
    r->cb = *cb;
    reset_election_timer(r);
    return 0;
}

int norn_raft_add_peer(norn_raft_t *r, uint32_t id, int voter)
{
    norn_raft_peer_t *p;
    if (!r || id == r->self_id || r->peer_count >= NORN_RAFT_MAX_PEERS) {
        return -1;
    }
    if (find_peer(r, id)) {
        return -1;
    }
    p = &r->peers[r->peer_count++];
    memset(p, 0, sizeof(*p));
    p->id = id;
    p->voter = voter ? 1 : 0;
    p->active = 1;
    p->next_index = last_index(r) + 1;
    return 0;
}

void norn_raft_tick(norn_raft_t *r, uint64_t now_ms)
{
    if (!r) {
        return;
    }
    r->now = now_ms;
    if (r->role == NORN_RAFT_LEADER) {
        if (now_ms >= r->heartbeat_deadline) {
            broadcast_append(r);
        }
        return;
    }
    if (r->role == NORN_RAFT_LEARNER) {
        return; /* learners never time out into elections */
    }
    /* follower or candidate: election timeout → (re)start a prevote round. */
    if (now_ms >= r->election_deadline) {
        start_prevote(r);
    }
}

static void handle_prevote_req(norn_raft_t *r, const norn_raft_msg_t *m)
{
    norn_raft_msg_t resp;
    int grant;
    /* PreVote never bumps our term. Grant iff: hypothetical term not stale, our
     * log not ahead, AND we believe the leader is gone — i.e. we have not heard a
     * valid leader for at least a full (minimum) election timeout. That last
     * clause is what stops a flapping node disrupting a healthy leader (AC4),
     * while at startup (never heard a leader) prevotes are granted freely. */
    grant = (m->term > r->current_term) &&
            log_up_to_date(r, m->last_log_term, m->last_log_index) &&
            (r->now - r->last_leader_contact >= r->election_min);
    memset(&resp, 0, sizeof(resp));
    resp.type = NORN_RAFT_MSG_PREVOTE_RESP;
    resp.to = m->from;
    resp.vote_granted = grant ? 1 : 0;
    send_msg(r, &resp);
}

static void handle_prevote_resp(norn_raft_t *r, const norn_raft_msg_t *m)
{
    if (!r->in_prevote) {
        return;
    }
    if (m->vote_granted) {
        r->prevotes++;
        if (r->prevotes >= quorum(r)) {
            become_candidate(r);
        }
    }
}

static void handle_vote_req(norn_raft_t *r, const norn_raft_msg_t *m)
{
    norn_raft_msg_t resp;
    int grant;
    /* step() has already adopted any newer term, so here m->term <= current. */
    grant = (m->term == r->current_term) &&
            (r->voted_for == -1 || r->voted_for == (int32_t)m->from) &&
            log_up_to_date(r, m->last_log_term, m->last_log_index);
    if (grant) {
        r->voted_for = (int32_t)m->from;
        persist(r);
        reset_election_timer(r);
    }
    memset(&resp, 0, sizeof(resp));
    resp.type = NORN_RAFT_MSG_VOTE_RESP;
    resp.to = m->from;
    resp.vote_granted = grant ? 1 : 0;
    send_msg(r, &resp);
}

static void handle_vote_resp(norn_raft_t *r, const norn_raft_msg_t *m)
{
    if (r->role != NORN_RAFT_CANDIDATE || m->term != r->current_term) {
        return;
    }
    if (m->vote_granted) {
        r->votes++;
        if (r->votes >= quorum(r)) {
            become_leader(r);
        }
    }
}

static void handle_append_req(norn_raft_t *r, const norn_raft_msg_t *m)
{
    norn_raft_msg_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.type = NORN_RAFT_MSG_APPEND_RESP;
    resp.to = m->from;

    if (m->term < r->current_term) {
        resp.success = 0; /* stale leader */
        send_msg(r, &resp);
        return;
    }
    /* valid leader for this (or a newer) term. */
    become_follower(r, m->term);
    r->leader_id = m->from;
    r->last_leader_contact = r->now;
    reset_election_timer(r);

    /* log-matching check. */
    if (m->prev_log_index > last_index(r) ||
        term_at(r, m->prev_log_index) != m->prev_log_term) {
        resp.success = 0;
        resp.match_index = last_index(r);
        send_msg(r, &resp);
        return;
    }
    if (m->has_entry) {
        uint32_t idx = m->prev_log_index + 1;
        uint32_t existing_term = term_at(r, idx);
        if (existing_term != 0 && existing_term != m->entry.term) {
            /* conflict: drop this and everything after it. */
            r->log_len = idx - r->base_index - 1;
        }
        if (idx > last_index(r)) {
            log_append(r, &m->entry);
        }
    }
    if (m->leader_commit > r->commit_index) {
        uint32_t li = last_index(r);
        r->commit_index = m->leader_commit < li ? m->leader_commit : li;
        apply_committed(r);
    }
    resp.success = 1;
    resp.match_index = last_index(r);
    send_msg(r, &resp);
}

static void handle_append_resp(norn_raft_t *r, const norn_raft_msg_t *m)
{
    norn_raft_peer_t *p;
    if (r->role != NORN_RAFT_LEADER || m->term != r->current_term) {
        return;
    }
    p = find_peer(r, m->from);
    if (!p) {
        return;
    }
    if (m->success) {
        if (m->match_index >= p->match_index) {
            p->match_index = m->match_index;
            p->next_index = m->match_index + 1;
        }
        leader_advance_commit(r);
        if (p->next_index <= last_index(r)) {
            send_append(r, p); /* stream the next entry immediately */
        }
    } else {
        if (p->next_index > r->base_index + 1) {
            p->next_index--;
        }
        send_append(r, p); /* retry lower */
    }
}

static void handle_timeout_now(norn_raft_t *r, const norn_raft_msg_t *m)
{
    if (m->term < r->current_term) {
        return;
    }
    /* step() has already adopted any newer term. Leadership transfer: skip
     * prevote + timer, stand immediately if eligible. */
    if (may_stand(r)) {
        become_candidate(r);
    }
}

void norn_raft_step(norn_raft_t *r, const norn_raft_msg_t *m)
{
    if (!r || !m || m->to != r->self_id) {
        return;
    }
    /* Any non-prevote message from a newer term moves us to that term first.
     * PreVote requests deliberately do NOT (their term is hypothetical). */
    if (m->type != NORN_RAFT_MSG_PREVOTE_REQ &&
        m->type != NORN_RAFT_MSG_PREVOTE_RESP && m->term > r->current_term) {
        become_follower(r, m->term);
    }
    switch (m->type) {
    case NORN_RAFT_MSG_PREVOTE_REQ:
        handle_prevote_req(r, m);
        break;
    case NORN_RAFT_MSG_PREVOTE_RESP:
        handle_prevote_resp(r, m);
        break;
    case NORN_RAFT_MSG_VOTE_REQ:
        handle_vote_req(r, m);
        break;
    case NORN_RAFT_MSG_VOTE_RESP:
        handle_vote_resp(r, m);
        break;
    case NORN_RAFT_MSG_APPEND_REQ:
        handle_append_req(r, m);
        break;
    case NORN_RAFT_MSG_APPEND_RESP:
        handle_append_resp(r, m);
        break;
    case NORN_RAFT_MSG_TIMEOUTNOW:
        handle_timeout_now(r, m);
        break;
    default: /* LCOV_EXCL_LINE: every norn_raft_msg_type_t is handled above */
        break; /* LCOV_EXCL_LINE */
    }
}

int64_t norn_raft_propose(norn_raft_t *r, const void *data, size_t len)
{
    norn_raft_entry_t e;
    if (!r || r->role != NORN_RAFT_LEADER || len > NORN_RAFT_MAX_ENTRY ||
        (len && !data) || r->log_len >= NORN_RAFT_MAX_LOG) {
        return -1;
    }
    memset(&e, 0, sizeof(e));
    e.type = NORN_RAFT_ENTRY_DATA;
    e.term = r->current_term;
    e.index = last_index(r) + 1;
    e.data_len = (uint16_t)len;
    if (len) {
        memcpy(e.data, data, len);
    }
    log_append(r, &e);
    leader_advance_commit(r); /* single-voter clusters commit immediately */
    broadcast_append(r);
    return (int64_t)e.index;
}

int64_t norn_raft_propose_config(norn_raft_t *r, norn_raft_entry_type_t type,
                                 uint32_t node, int voter)
{
    norn_raft_entry_t e;
    if (!r || r->role != NORN_RAFT_LEADER || !is_config((uint8_t)type) ||
        r->log_len >= NORN_RAFT_MAX_LOG) {
        return -1;
    }
    memset(&e, 0, sizeof(e));
    e.type = (uint8_t)type;
    e.term = r->current_term;
    e.index = last_index(r) + 1;
    e.cfg_node = node;
    e.cfg_voter = voter ? 1 : 0;
    log_append(r, &e); /* takes effect immediately (Ongaro §4.1) */
    leader_advance_commit(r);
    broadcast_append(r);
    return (int64_t)e.index;
}

int norn_raft_snapshot(norn_raft_t *r, uint32_t index)
{
    uint32_t drop, i;
    if (!r || index <= r->base_index || index > r->commit_index ||
        index > last_index(r)) {
        return -1;
    }
    r->base_term = term_at(r, index);
    drop = index - r->base_index;
    for (i = 0; i + drop < r->log_len; i++) {
        r->log[i] = r->log[i + drop];
    }
    r->log_len -= drop;
    r->base_index = index;
    if (r->last_applied < index) { /* LCOV_EXCL_BR_LINE: last_applied already tracks commit */
        r->last_applied = index; /* LCOV_EXCL_LINE */
    }
    return 0;
}

norn_raft_role_t norn_raft_role(const norn_raft_t *r)
{
    return r ? r->role : NORN_RAFT_FOLLOWER;
}
uint32_t norn_raft_term(const norn_raft_t *r) { return r ? r->current_term : 0; }
uint32_t norn_raft_commit_index(const norn_raft_t *r)
{
    return r ? r->commit_index : 0;
}
uint32_t norn_raft_last_index(const norn_raft_t *r)
{
    return r ? last_index(r) : 0;
}
uint32_t norn_raft_leader(const norn_raft_t *r) { return r ? r->leader_id : 0; }
int norn_raft_is_voter(const norn_raft_t *r) { return r ? r->self_voter : 0; }
uint32_t norn_raft_peer_count(const norn_raft_t *r)
{
    uint32_t n = 0, i;
    if (!r) {
        return 0;
    }
    for (i = 0; i < r->peer_count; i++) {
        if (r->peers[i].active) {
            n++;
        }
    }
    return n;
}
