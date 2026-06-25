/* Unit tests for the pure Raft core (FEAT-024), norn_raft.
 *
 * Drives an in-memory multi-node simulation: nodes are ticked on a shared
 * clock and their RPCs flow through a message queue with deep-copied entries.
 * No sockets, no real time. Aims for 100% line + branch coverage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "norn_raft.h"

#define MAXN 8
#define QCAP 8192

typedef struct {
    raft_node_id_t id;
    raft_t *r;
    int down;                 /* partitioned: messages to/from are dropped */
    unsigned char applied[8192];
    size_t applied_len;
    int apply_count;
} node_t;

typedef struct {
    raft_node_id_t from, dest;
    raft_msg_t msg;
    raft_entry_t ents[RAFT_MAX_BATCH];
} qitem_t;

static node_t g_nodes[MAXN];
static int g_n;
static qitem_t g_q[QCAP];
static int g_qhead, g_qcount;
static uint64_t g_clock;

static node_t *node_by_id(raft_node_id_t id) {
    for (int i = 0; i < g_n; i++)
        if (g_nodes[i].id == id) return &g_nodes[i];
    return NULL;
}

static void h_send(void *ctx, raft_node_id_t dest, const raft_msg_t *msg) {
    node_t *self = ctx;
    assert(g_qcount < QCAP);
    qitem_t *it = &g_q[(g_qhead + g_qcount) % QCAP];
    g_qcount++;
    it->from = self->id;
    it->dest = dest;
    it->msg = *msg;
    if (msg->type == RAFT_MSG_APPEND && msg->n_entries) {
        size_t n = msg->n_entries;
        for (size_t i = 0; i < n; i++) it->ents[i] = msg->entries[i];
        it->msg.entries = it->ents;
        it->msg.n_entries = n;
    } else {
        it->msg.entries = NULL;
        it->msg.n_entries = 0;
    }
}

static void h_apply(void *ctx, const raft_entry_t *e) {
    node_t *self = ctx;
    memcpy(self->applied + self->applied_len, e->data, e->len);
    self->applied_len += e->len;
    self->apply_count++;
}

static void deliver_all(void) {
    while (g_qcount > 0) {
        qitem_t it = g_q[g_qhead];
        g_qhead = (g_qhead + 1) % QCAP;
        g_qcount--;
        node_t *from = node_by_id(it.from);
        node_t *dest = node_by_id(it.dest);
        if (!dest || dest->down || (from && from->down)) continue;
        if (it.msg.type == RAFT_MSG_APPEND && it.msg.n_entries)
            it.msg.entries = it.ents;
        raft_recv(dest->r, it.from, &it.msg);
    }
}

static void step(void) {
    for (int i = 0; i < g_n; i++)
        if (!g_nodes[i].down) raft_tick(g_nodes[i].r, g_clock);
    deliver_all();
    g_clock += 1; /* 1ms granularity so the per-id timeout spread separates */
}

static void steps(int n) {
    for (int i = 0; i < n; i++) step();
}

/* Build a cluster: every node knows every member. roles[]/elig[] index by
 * position; ids are 1..n. */
static void cluster(int n, const raft_member_role_t *roles, const int *elig) {
    memset(g_nodes, 0, sizeof(g_nodes));
    g_q[0].from = 0;
    g_qhead = g_qcount = 0;
    g_clock = 0;
    g_n = n;
    /* Small timeouts (base 20ms, 1ms per-id spread, 5ms heartbeat) so the
     * lowest-id eligible voter wins deterministically under 1ms ticks. */
    raft_config_t cfg = {.election_base_ms = 20, .election_spread_ms = 60, .heartbeat_ms = 5};
    for (int i = 0; i < n; i++) {
        g_nodes[i].id = (raft_node_id_t)(i + 1);
        raft_io_t io = {h_send, h_apply, &g_nodes[i]};
        g_nodes[i].r = raft_new(g_nodes[i].id, &io, &cfg);
        assert(g_nodes[i].r);
    }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            assert(raft_add_member(g_nodes[i].r, (raft_node_id_t)(j + 1),
                                   roles[j], elig[j]) == 0);
}

static void cluster_free(void) {
    for (int i = 0; i < g_n; i++) raft_free(g_nodes[i].r);
}

static node_t *the_leader(void) {
    node_t *l = NULL;
    for (int i = 0; i < g_n; i++)
        if (!g_nodes[i].down && raft_role(g_nodes[i].r) == RAFT_LEADER) {
            assert(l == NULL); /* at most one leader among live nodes */
            l = &g_nodes[i];
        }
    return l;
}

/* ---- tests ---- */

static void test_single_voter_self_elects(void) {
    raft_member_role_t roles[] = {RAFT_VOTER};
    int elig[] = {1};
    cluster(1, roles, elig);
    steps(40);
    assert(raft_role(g_nodes[0].r) == RAFT_LEADER);
    assert(raft_term(g_nodes[0].r) == 1);
    assert(raft_leader(g_nodes[0].r) == 1);
    cluster_free();
}

static void test_three_voters_elect_and_replicate(void) {
    raft_member_role_t roles[] = {RAFT_VOTER, RAFT_VOTER, RAFT_VOTER};
    int elig[] = {1, 1, 1};
    cluster(3, roles, elig);
    steps(60);

    node_t *l = the_leader();
    assert(l && l->id == 1); /* lowest-id eligible voter wins deterministically */
    for (int i = 0; i < g_n; i++)
        assert(raft_term(g_nodes[i].r) == raft_term(l->r));

    uint64_t idx = raft_propose(l->r, "hello", 5);
    assert(idx > 0);
    steps(20);

    for (int i = 0; i < g_n; i++) {
        assert(raft_commit_index(g_nodes[i].r) >= idx);
        assert(g_nodes[i].applied_len == 5);
        assert(memcmp(g_nodes[i].applied, "hello", 5) == 0);
    }
    cluster_free();
}

static void test_learner_replicates_but_not_quorum(void) {
    raft_member_role_t roles[] = {RAFT_VOTER, RAFT_VOTER, RAFT_VOTER, RAFT_LEARNER};
    int elig[] = {1, 1, 1, 0};
    cluster(4, roles, elig);
    steps(60);
    node_t *l = the_leader();
    assert(l);
    assert(!raft_is_voter(l->r, 4)); /* node 4 is a learner */

    uint64_t idx = raft_propose(l->r, "x", 1);
    steps(20);
    /* The learner replicates and applies the entry. */
    assert(raft_commit_index(node_by_id(4)->r) >= idx);
    assert(node_by_id(4)->applied_len == 1);

    /* With voters 1,2 up and voter 3 + learner 4 down, a quorum of voters (2/3)
     * remains, so writes still commit. */
    node_by_id(3)->down = 1;
    node_by_id(4)->down = 1;
    uint64_t idx2 = raft_propose(l->r, "y", 1);
    steps(30);
    assert(raft_commit_index(l->r) >= idx2);
    cluster_free();
}

static void test_majority_required(void) {
    raft_member_role_t roles[] = {RAFT_VOTER, RAFT_VOTER, RAFT_VOTER};
    int elig[] = {1, 1, 1};
    cluster(3, roles, elig);
    steps(60);
    node_t *l = the_leader();
    assert(l);

    /* Kill two of three voters: no quorum, so a new proposal cannot commit. */
    node_by_id(2)->down = 1;
    node_by_id(3)->down = 1;
    uint64_t before = raft_commit_index(l->r);
    uint64_t idx = raft_propose(l->r, "z", 1);
    steps(40);
    assert(idx > before);
    assert(raft_commit_index(l->r) == before); /* stuck, as Raft requires */
    cluster_free();
}

static void test_non_eligible_never_leads(void) {
    raft_member_role_t roles[] = {RAFT_VOTER, RAFT_VOTER, RAFT_VOTER};
    int elig[] = {0, 1, 1}; /* node 1 is a voter but not election-eligible */
    cluster(3, roles, elig);
    steps(80);
    node_t *l = the_leader();
    assert(l && l->id == 2); /* lowest *eligible* voter leads */
    assert(raft_role(node_by_id(1)->r) != RAFT_LEADER);
    cluster_free();
}

static void test_prevote_isolated_no_term_bump(void) {
    raft_member_role_t roles[] = {RAFT_VOTER, RAFT_VOTER, RAFT_VOTER};
    int elig[] = {1, 1, 1};
    cluster(3, roles, elig);
    steps(60);
    node_t *l = the_leader();
    assert(l && l->id == 1);
    uint64_t term = raft_term(l->r);

    /* Isolate a follower; its PreVote rounds get no responses, so it must not
     * bump the cluster term (the whole point of PreVote). */
    node_t *iso = node_by_id(3);
    iso->down = 1;
    uint64_t iso_term = raft_term(iso->r);
    steps(80);
    assert(raft_term(iso->r) == iso_term); /* no real election started */

    /* Heal: it rejoins, learns the leader, stays a follower at the live term. */
    iso->down = 0;
    steps(40);
    assert(raft_role(iso->r) == RAFT_FOLLOWER);
    assert(raft_term(iso->r) == term);
    assert(the_leader()->id == 1);
    cluster_free();
}

static void test_leader_failover(void) {
    raft_member_role_t roles[] = {RAFT_VOTER, RAFT_VOTER, RAFT_VOTER};
    int elig[] = {1, 1, 1};
    cluster(3, roles, elig);
    steps(60);
    node_t *l = the_leader();
    assert(l && l->id == 1);

    /* Leader dies; a remaining eligible voter must take over. */
    l->down = 1;
    steps(120);
    node_t *l2 = the_leader();
    assert(l2 && l2->id != 1);
    assert(raft_term(l2->r) > raft_term(l->r));

    /* New leader still serves writes. */
    uint64_t idx = raft_propose(l2->r, "after", 5);
    steps(30);
    assert(raft_commit_index(l2->r) >= idx);
    cluster_free();
}

static void test_log_catchup_after_partition(void) {
    raft_member_role_t roles[] = {RAFT_VOTER, RAFT_VOTER, RAFT_VOTER};
    int elig[] = {1, 1, 1};
    cluster(3, roles, elig);
    steps(60);
    node_t *l = the_leader();
    assert(l && l->id == 1);

    /* Partition node 3, commit several entries via the 1,2 quorum. */
    node_by_id(3)->down = 1;
    for (int k = 0; k < 5; k++) {
        raft_propose(l->r, "ab", 2);
        steps(10);
    }
    uint64_t lead_commit = raft_commit_index(l->r);

    /* Heal: the lagging node catches up via AppendEntries backtracking. */
    node_by_id(3)->down = 0;
    steps(60);
    assert(raft_commit_index(node_by_id(3)->r) >= lead_commit);
    assert(node_by_id(3)->applied_len == node_by_id(1)->applied_len);
    cluster_free();
}

static void test_membership_api(void) {
    raft_member_role_t roles[] = {RAFT_VOTER};
    int elig[] = {1};
    cluster(1, roles, elig);
    raft_t *r = g_nodes[0].r;

    assert(raft_add_member(r, 2, RAFT_LEARNER, 0) == 0);
    assert(raft_add_member(r, 2, RAFT_LEARNER, 0) == -1); /* dup */
    assert(raft_add_member(r, RAFT_NO_NODE, RAFT_VOTER, 1) == -1);
    assert(raft_is_voter(r, 2) == 0);
    assert(raft_promote(r, 2) == 0);
    assert(raft_is_voter(r, 2) == 1);
    assert(raft_promote(r, 2) == -1);      /* already a voter */
    assert(raft_promote(r, 99) == -1);     /* absent */
    assert(raft_set_eligible(r, 2, 0) == 0);
    assert(raft_set_eligible(r, 99, 0) == -1);
    assert(raft_remove_member(r, 2) == 0);
    assert(raft_remove_member(r, 2) == -1);
    assert(raft_is_voter(r, 2) == 0);

    /* fill to capacity to exercise the full-table branch */
    int added = 1; /* node 1 already present */
    for (raft_node_id_t id = 2; added < RAFT_MAX_NODES; id++, added++)
        assert(raft_add_member(r, id, RAFT_LEARNER, 0) == 0);
    assert(raft_add_member(r, 999, RAFT_LEARNER, 0) == -1); /* full */
    cluster_free();
}

static void test_null_and_error_paths(void) {
    raft_io_t io = {h_send, h_apply, &g_nodes[0]};
    assert(raft_new(RAFT_NO_NODE, &io, NULL) == NULL);
    assert(raft_new(1, NULL, NULL) == NULL);

    /* Custom config is honoured (non-default values exercise the cfg path). */
    raft_config_t cfg = {.election_base_ms = 100, .election_spread_ms = 50, .heartbeat_ms = 20};
    raft_t *r = raft_new(1, &io, &cfg);
    assert(r);

    /* propose on a non-leader fails; oversized fails. */
    assert(raft_propose(r, "x", 1) == 0);
    unsigned char big[RAFT_ENTRY_MAX + 1] = {0};
    assert(raft_add_member(r, 1, RAFT_VOTER, 1) == 0);
    /* become leader */
    g_nodes[0].id = 1;
    g_nodes[0].r = r;
    g_n = 1;
    for (int i = 0; i < 30; i++) raft_tick(r, (uint64_t)i * 10);
    assert(raft_role(r) == RAFT_LEADER);
    assert(raft_propose(r, big, sizeof(big)) == 0);    /* too big */
    assert(raft_propose(r, NULL, 0) > 0);               /* empty command ok */

    /* NULL-safety on every public entry point. */
    assert(raft_role(NULL) == RAFT_FOLLOWER);
    assert(raft_leader(NULL) == RAFT_NO_NODE);
    assert(raft_term(NULL) == 0);
    assert(raft_commit_index(NULL) == 0);
    assert(raft_last_log_index(NULL) == 0);
    assert(raft_is_voter(NULL, 1) == 0);
    assert(raft_propose(NULL, "x", 1) == 0);
    assert(raft_add_member(NULL, 1, RAFT_VOTER, 1) == -1);
    assert(raft_remove_member(NULL, 1) == -1);
    assert(raft_promote(NULL, 1) == -1);
    assert(raft_set_eligible(NULL, 1, 1) == -1);
    raft_recv(NULL, 1, NULL);
    raft_recv(r, 1, NULL);
    raft_tick(NULL, 0);
    raft_free(NULL);
    raft_free(r);
}

/* ---- white-box handler tests (crafted RPCs, single node) ---- */

static raft_msg_t g_sent;
static raft_node_id_t g_sent_dest;
static int g_sent_count;
static unsigned char g_wb_applied[8192];
static size_t g_wb_applied_len;

static void wb_send(void *ctx, raft_node_id_t dest, const raft_msg_t *m) {
    (void)ctx;
    g_sent = *m;
    g_sent_dest = dest;
    g_sent_count++;
}
static void wb_apply(void *ctx, const raft_entry_t *e) {
    (void)ctx;
    memcpy(g_wb_applied + g_wb_applied_len, e->data, e->len);
    g_wb_applied_len += e->len;
}

/* A node (id 1) that knows voters {1,2,3}. */
static raft_t *wb_node(void) {
    g_sent_count = 0;
    g_wb_applied_len = 0;
    static raft_io_t io;
    io.send = wb_send;
    io.apply = wb_apply;
    io.ctx = NULL;
    raft_config_t cfg = {.election_base_ms = 20, .election_spread_ms = 60, .heartbeat_ms = 5};
    raft_t *r = raft_new(1, &io, &cfg);
    assert(r);
    assert(raft_add_member(r, 1, RAFT_VOTER, 1) == 0);
    assert(raft_add_member(r, 2, RAFT_VOTER, 1) == 0);
    assert(raft_add_member(r, 3, RAFT_VOTER, 1) == 0);
    return r;
}

static raft_msg_t mk_rv(uint64_t term, int pre, raft_node_id_t cand, uint64_t lli, uint64_t llt) {
    raft_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = RAFT_MSG_REQUEST_VOTE;
    m.term = term;
    m.pre_vote = pre;
    m.candidate_id = cand;
    m.last_log_index = lli;
    m.last_log_term = llt;
    return m;
}
static raft_msg_t mk_ae(uint64_t term, raft_node_id_t leader, uint64_t pli, uint64_t plt,
                        const raft_entry_t *e, size_t n, uint64_t lc) {
    raft_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = RAFT_MSG_APPEND;
    m.term = term;
    m.leader_id = leader;
    m.prev_log_index = pli;
    m.prev_log_term = plt;
    m.entries = e;
    m.n_entries = n;
    m.leader_commit = lc;
    return m;
}
static raft_msg_t mk_resp(raft_msg_type_t t, uint64_t term) {
    raft_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = t;
    m.term = term;
    return m;
}

static void test_wb_request_vote(void) {
    raft_t *r = wb_node();
    /* Reach term 5 via a heartbeat from leader 2 (bumps term). */
    raft_msg_t hb = mk_ae(5, 2, 0, 0, NULL, 0, 0);
    raft_recv(r, 2, &hb);
    assert(raft_term(r) == 5 && raft_role(r) == RAFT_FOLLOWER);
    /* Second heartbeat at the same term: become_follower without a term bump. */
    raft_recv(r, 2, &hb);
    assert(raft_term(r) == 5);

    /* Real vote with a stale term → reject. */
    raft_msg_t lo = mk_rv(4, 0, 3, 0, 0);
    raft_recv(r, 3, &lo);
    assert(g_sent.type == RAFT_MSG_REQUEST_VOTE_RESP && g_sent.vote_granted == 0);

    /* PreVote while we have recently heard from a leader → deny. */
    raft_msg_t pv = mk_rv(6, 1, 3, 0, 0);
    raft_recv(r, 3, &pv);
    assert(g_sent.pre_vote == 1 && g_sent.vote_granted == 0);

    /* With election_elapsed in [base, timeout) we have not "heard from a leader"
     * but have not yet started our own election → PreVote is granted. */
    raft_tick(r, 20);
    assert(raft_role(r) == RAFT_FOLLOWER);
    raft_recv(r, 3, &pv);
    assert(g_sent.pre_vote == 1 && g_sent.vote_granted == 1);
    assert(raft_term(r) == 5); /* pre-vote never bumps our term */

    /* Real vote → grant and record. */
    raft_msg_t v = mk_rv(6, 0, 3, 0, 0);
    raft_recv(r, 3, &v);
    assert(g_sent.vote_granted == 1 && raft_term(r) == 6);
    /* Same term, different candidate → reject (already voted). */
    raft_msg_t v2 = mk_rv(6, 0, 2, 0, 0);
    raft_recv(r, 2, &v2);
    assert(g_sent.vote_granted == 0);
    /* Same candidate again → idempotent grant. */
    raft_recv(r, 3, &v);
    assert(g_sent.vote_granted == 1);
    /* Candidate with a shorter log than ours → deny on up-to-date check. */
    raft_entry_t e = {.term = 6, .index = 1, .type = RAFT_ENTRY_NORMAL, .len = 1};
    raft_msg_t put = mk_ae(6, 3, 0, 0, &e, 1, 0);
    raft_recv(r, 3, &put); /* now we have a log entry at term 6 */
    raft_msg_t behind = mk_rv(7, 1, 2, 0, 0);
    raft_tick(r, 5000);
    raft_recv(r, 2, &behind);
    assert(g_sent.vote_granted == 0);
    raft_free(r);
}

static void test_wb_append(void) {
    raft_t *r = wb_node();
    raft_msg_t hb = mk_ae(5, 2, 0, 0, NULL, 0, 0);
    raft_recv(r, 2, &hb);

    /* Stale-term append → reject. */
    raft_msg_t stale = mk_ae(4, 2, 0, 0, NULL, 0, 0);
    raft_recv(r, 2, &stale);
    assert(g_sent.type == RAFT_MSG_APPEND_RESP && g_sent.success == 0);

    /* Append two entries and commit them. */
    raft_entry_t e[2];
    memset(e, 0, sizeof(e));
    e[0].term = 5; e[0].type = RAFT_ENTRY_NORMAL; e[0].data[0] = 'A'; e[0].len = 1;
    e[1].term = 5; e[1].type = RAFT_ENTRY_NORMAL; e[1].data[0] = 'B'; e[1].len = 1;
    raft_msg_t ap = mk_ae(5, 2, 0, 0, e, 2, 2);
    raft_recv(r, 2, &ap);
    assert(g_sent.success == 1 && raft_last_log_index(r) == 2);
    assert(raft_commit_index(r) == 2);
    assert(g_wb_applied_len == 2 && memcmp(g_wb_applied, "AB", 2) == 0);

    /* prev_log_index beyond our log → reject. */
    raft_msg_t far = mk_ae(5, 2, 9, 5, NULL, 0, 0);
    raft_recv(r, 2, &far);
    assert(g_sent.success == 0);
    /* prev_log_term mismatch → reject. */
    raft_msg_t mm = mk_ae(5, 2, 1, 9, NULL, 0, 0);
    raft_recv(r, 2, &mm);
    assert(g_sent.success == 0);

    /* Re-send an already-present entry (same idx+term) → skipped, still success. */
    raft_msg_t dupe = mk_ae(5, 2, 0, 0, e, 1, 0);
    raft_recv(r, 2, &dupe);
    assert(g_sent.success == 1 && raft_last_log_index(r) == 2);

    /* leader_commit larger than our log is clamped to last index. */
    raft_msg_t hc = mk_ae(5, 2, 2, 5, NULL, 0, 99);
    raft_recv(r, 2, &hc);
    assert(raft_commit_index(r) == 2);
    raft_free(r);
}

static void test_wb_conflict_truncation(void) {
    raft_t *r = wb_node();
    raft_msg_t hb = mk_ae(5, 2, 0, 0, NULL, 0, 0);
    raft_recv(r, 2, &hb);
    /* Two entries, uncommitted. */
    raft_entry_t e[2];
    memset(e, 0, sizeof(e));
    e[0].term = 5; e[0].type = RAFT_ENTRY_NORMAL; e[0].data[0] = 'A'; e[0].len = 1;
    e[1].term = 5; e[1].type = RAFT_ENTRY_NORMAL; e[1].data[0] = 'B'; e[1].len = 1;
    raft_recv(r, 2, &(raft_msg_t){0}); /* no-op guard to keep g_sent defined */
    raft_msg_t ap = mk_ae(5, 2, 0, 0, e, 2, 0);
    raft_recv(r, 2, &ap);
    assert(raft_last_log_index(r) == 2);
    /* A newer leader overwrites index 2 with a higher-term entry: conflict →
     * truncate the tail and append. */
    raft_entry_t c = {.term = 6, .type = RAFT_ENTRY_NORMAL, .len = 1};
    c.data[0] = 'C';
    raft_msg_t apc = mk_ae(6, 3, 1, 5, &c, 1, 0);
    raft_recv(r, 3, &apc);
    assert(g_sent.success == 1 && raft_last_log_index(r) == 2);
    raft_free(r);
}

static void test_wb_append_resp_and_leader(void) {
    raft_t *r = wb_node();
    /* Drive node 1 to leadership by feeding granted (pre)votes from node 2. */
    raft_tick(r, 1000);
    assert(raft_role(r) == RAFT_CANDIDATE);
    raft_msg_t pvr = mk_resp(RAFT_MSG_REQUEST_VOTE_RESP, raft_term(r) + 1);
    pvr.pre_vote = 1;
    pvr.vote_granted = 1;
    raft_recv(r, 2, &pvr); /* pre-vote quorum → real election */
    raft_msg_t vr = mk_resp(RAFT_MSG_REQUEST_VOTE_RESP, raft_term(r));
    vr.vote_granted = 1;
    raft_recv(r, 2, &vr); /* real quorum → leader */
    assert(raft_role(r) == RAFT_LEADER);

    /* A stale RequestVoteResp while leader is ignored (not candidate). */
    raft_recv(r, 3, &vr);
    assert(raft_role(r) == RAFT_LEADER);

    /* AppendResp success from node 2 → quorum on the NOOP → commit advances. */
    raft_msg_t sr = mk_resp(RAFT_MSG_APPEND_RESP, raft_term(r));
    sr.success = 1;
    sr.match_index = raft_last_log_index(r);
    raft_recv(r, 2, &sr);
    assert(raft_commit_index(r) >= 1);

    /* A success from node 3 advances its next_index past 1 so a subsequent
     * failure exercises the back-off-and-resend branch. */
    raft_recv(r, 3, &sr);
    raft_msg_t fr = mk_resp(RAFT_MSG_APPEND_RESP, raft_term(r));
    fr.success = 0;
    g_sent_count = 0;
    raft_recv(r, 3, &fr);
    assert(g_sent_count >= 1 && g_sent.type == RAFT_MSG_APPEND);

    /* AppendResp from an unknown member → ignored. */
    raft_recv(r, 99, &sr);
    /* AppendResp with a higher term → step down to follower. */
    raft_msg_t hr = mk_resp(RAFT_MSG_APPEND_RESP, raft_term(r) + 9);
    raft_recv(r, 3, &hr);
    assert(raft_role(r) == RAFT_FOLLOWER);

    /* A late RequestVoteResp with a higher term also steps a candidate down. */
    raft_tick(r, 100000);
    assert(raft_role(r) == RAFT_CANDIDATE);
    raft_msg_t hv = mk_resp(RAFT_MSG_REQUEST_VOTE_RESP, raft_term(r) + 50);
    hv.vote_granted = 0;
    raft_recv(r, 2, &hv);
    assert(raft_role(r) == RAFT_FOLLOWER);
    raft_free(r);
}

static void test_wb_append_resp_on_follower(void) {
    /* AppendResp delivered to a non-leader is ignored without effect. */
    raft_t *r = wb_node();
    raft_msg_t sr = mk_resp(RAFT_MSG_APPEND_RESP, 0);
    sr.success = 1;
    raft_recv(r, 2, &sr);
    assert(raft_role(r) == RAFT_FOLLOWER);
    raft_free(r);
}

static void test_wb_batch_over_limit(void) {
    /* A follower far behind is caught up in RAFT_MAX_BATCH-sized chunks. */
    raft_member_role_t roles[] = {RAFT_VOTER, RAFT_VOTER, RAFT_VOTER};
    int elig[] = {1, 1, 1};
    cluster(3, roles, elig);
    steps(60);
    node_t *l = the_leader();
    assert(l);
    node_by_id(3)->down = 1;
    for (int k = 0; k < RAFT_MAX_BATCH + 4; k++) {
        raft_propose(l->r, "q", 1);
        steps(8);
    }
    node_by_id(3)->down = 0;
    steps(80);
    assert(raft_commit_index(node_by_id(3)->r) == raft_commit_index(l->r));
    cluster_free();
}

static void test_wb_null_io_and_cfg(void) {
    /* NULL send/apply callbacks must be tolerated (sole voter self-elects,
     * proposes, commits — exercising the "no callback" branches). */
    raft_io_t io = {NULL, NULL, NULL};
    raft_t *r = raft_new(1, &io, NULL); /* NULL cfg → defaults */
    assert(r);
    assert(raft_add_member(r, 1, RAFT_VOTER, 1) == 0);
    for (int i = 0; i < 400; i++) raft_tick(r, (uint64_t)i * 10);
    assert(raft_role(r) == RAFT_LEADER);
    uint64_t idx = raft_propose(r, "k", 1);
    assert(idx > 0);
    raft_tick(r, 5000);
    assert(raft_commit_index(r) >= idx);
    /* propose with len>0 but NULL data → entry reserved, no copy. */
    assert(raft_propose(r, NULL, 3) > 0);
    raft_free(r);

    /* A NULL send callback must also be tolerated when there *is* a peer to send
     * to: ticking a two-member NULL-io node broadcasts a (dropped) vote. */
    raft_t *r2 = raft_new(1, &io, NULL);
    raft_add_member(r2, 1, RAFT_VOTER, 1);
    raft_add_member(r2, 2, RAFT_VOTER, 1);
    for (int i = 0; i < 60; i++) raft_tick(r2, (uint64_t)i * 10);
    assert(raft_role(r2) == RAFT_CANDIDATE); /* no responses ⇒ stuck pre-voting */
    raft_free(r2);
}

static void test_wb_self_not_eligible_shapes(void) {
    raft_config_t cfg = {.election_base_ms = 20, .election_spread_ms = 60, .heartbeat_ms = 5};
    raft_io_t io = {wb_send, wb_apply, NULL};

    /* self is a learner → never stands for election. */
    raft_t *r = raft_new(1, &io, &cfg);
    raft_add_member(r, 1, RAFT_LEARNER, 0);
    raft_add_member(r, 2, RAFT_VOTER, 1);
    for (int i = 0; i < 50; i++) raft_tick(r, (uint64_t)i * 10);
    assert(raft_role(r) != RAFT_LEADER);
    raft_free(r);

    /* self is not in the member set → start_election finds no self member. */
    r = raft_new(1, &io, &cfg);
    raft_add_member(r, 2, RAFT_VOTER, 1);
    raft_add_member(r, 3, RAFT_VOTER, 1);
    for (int i = 0; i < 50; i++) raft_tick(r, (uint64_t)i * 10);
    assert(raft_role(r) != RAFT_LEADER);
    raft_free(r);
}

static void test_wb_vote_edges(void) {
    raft_t *r = wb_node();
    raft_msg_t hb = mk_ae(5, 2, 0, 0, NULL, 0, 0);
    raft_recv(r, 2, &hb);
    raft_tick(r, 20); /* election_elapsed in [base,timeout) so pre-vote can grant */

    /* PreVote with term <= ours → denied (first conjunct false). */
    raft_msg_t pv_stale = mk_rv(5, 1, 3, 0, 0);
    raft_recv(r, 3, &pv_stale);
    assert(g_sent.pre_vote == 1 && g_sent.vote_granted == 0);

    /* Give ourselves a log entry, then a real vote with an empty (behind) log is
     * denied on the up-to-date check. */
    raft_entry_t e = {.term = 5, .type = RAFT_ENTRY_NORMAL, .len = 1};
    raft_msg_t put = mk_ae(5, 2, 0, 0, &e, 1, 0);
    raft_recv(r, 2, &put);
    raft_msg_t behind = mk_rv(9, 0, 3, 0, 0);
    raft_recv(r, 3, &behind);
    assert(g_sent.vote_granted == 0);

    /* RequestVoteResp from a non-member is ignored. */
    raft_tick(r, 100000);
    assert(raft_role(r) == RAFT_CANDIDATE);
    raft_msg_t rogue = mk_resp(RAFT_MSG_REQUEST_VOTE_RESP, raft_term(r));
    rogue.pre_vote = 1;
    rogue.vote_granted = 1;
    raft_recv(r, 99, &rogue);
    raft_free(r);
}

static void test_wb_subquorum_tally(void) {
    /* 5 voters: a single pre-vote grant is not yet a quorum (the `< quorum`
     * early-return), and the real round likewise needs a majority. */
    raft_config_t cfg = {.election_base_ms = 20, .election_spread_ms = 60, .heartbeat_ms = 5};
    raft_io_t io = {wb_send, wb_apply, NULL};
    raft_t *r = raft_new(1, &io, &cfg);
    for (raft_node_id_t id = 1; id <= 5; id++) raft_add_member(r, id, RAFT_VOTER, 1);
    raft_tick(r, 1000);
    assert(raft_role(r) == RAFT_CANDIDATE);

    raft_msg_t g = mk_resp(RAFT_MSG_REQUEST_VOTE_RESP, raft_term(r) + 1);
    g.pre_vote = 1;
    g.vote_granted = 1;
    raft_recv(r, 2, &g); /* grants = 2 (self+1) < quorum 3 → still pre-voting */
    assert(raft_role(r) == RAFT_CANDIDATE);
    raft_recv(r, 3, &g); /* grants = 3 → real election begins */
    raft_msg_t rg = mk_resp(RAFT_MSG_REQUEST_VOTE_RESP, raft_term(r));
    rg.vote_granted = 1;
    raft_recv(r, 4, &rg); /* real grants = 2 < 3 → not leader yet */
    assert(raft_role(r) == RAFT_CANDIDATE);
    raft_recv(r, 5, &rg); /* = 3 → leader */
    assert(raft_role(r) == RAFT_LEADER);
    raft_free(r);
}

static void test_wb_next_index_floor(void) {
    /* A failure response when next_index is already 1 cannot back off further. */
    raft_t *r = wb_node();
    raft_tick(r, 1000);
    raft_msg_t pvr = mk_resp(RAFT_MSG_REQUEST_VOTE_RESP, raft_term(r) + 1);
    pvr.pre_vote = 1;
    pvr.vote_granted = 1;
    raft_recv(r, 2, &pvr);
    raft_msg_t vr = mk_resp(RAFT_MSG_REQUEST_VOTE_RESP, raft_term(r));
    vr.vote_granted = 1;
    raft_recv(r, 2, &vr);
    assert(raft_role(r) == RAFT_LEADER);
    /* next_index for node 3 is 1 (NOOP at idx1). A failure must NOT resend. */
    raft_msg_t fr = mk_resp(RAFT_MSG_APPEND_RESP, raft_term(r));
    fr.success = 0;
    g_sent_count = 0;
    raft_recv(r, 3, &fr);
    assert(g_sent_count == 0);
    raft_free(r);
}

static void test_wb_commit_term_rule(void) {
    /* A leader may only commit an entry of its current term by counting (Raft
     * §5.4.2); the commit scan breaks at an older-term entry. */
    raft_t *r = wb_node();
    raft_entry_t e[2];
    memset(e, 0, sizeof(e));
    e[0].term = 1; e[0].type = RAFT_ENTRY_NORMAL; e[0].len = 1; e[0].data[0] = '1';
    e[1].term = 1; e[1].type = RAFT_ENTRY_NORMAL; e[1].len = 1; e[1].data[0] = '2';
    raft_msg_t ap = mk_ae(1, 2, 0, 0, e, 2, 0); /* term-1 entries, uncommitted */
    raft_recv(r, 2, &ap);
    assert(raft_last_log_index(r) == 2 && raft_term(r) == 1);

    /* Win term 2; become_leader appends a term-2 NOOP at index 3. */
    raft_tick(r, 50);
    raft_msg_t pvr = mk_resp(RAFT_MSG_REQUEST_VOTE_RESP, 0);
    pvr.pre_vote = 1;
    pvr.vote_granted = 1;
    raft_recv(r, 2, &pvr);
    raft_msg_t vr = mk_resp(RAFT_MSG_REQUEST_VOTE_RESP, raft_term(r));
    vr.vote_granted = 1;
    raft_recv(r, 2, &vr);
    assert(raft_role(r) == RAFT_LEADER && raft_term(r) == 2 && raft_last_log_index(r) == 3);

    /* node 2 acks only up to index 2: the term-2 NOOP lacks a quorum, and the
     * scan must break at index 2 (term 1) without committing. */
    raft_msg_t s = mk_resp(RAFT_MSG_APPEND_RESP, raft_term(r));
    s.success = 1;
    s.match_index = 2;
    raft_recv(r, 2, &s);
    assert(raft_commit_index(r) == 0);
    raft_free(r);
}

int main(void) {
    test_single_voter_self_elects();
    test_three_voters_elect_and_replicate();
    test_learner_replicates_but_not_quorum();
    test_majority_required();
    test_non_eligible_never_leads();
    test_prevote_isolated_no_term_bump();
    test_leader_failover();
    test_log_catchup_after_partition();
    test_membership_api();
    test_null_and_error_paths();
    test_wb_request_vote();
    test_wb_append();
    test_wb_conflict_truncation();
    test_wb_append_resp_and_leader();
    test_wb_append_resp_on_follower();
    test_wb_batch_over_limit();
    test_wb_null_io_and_cfg();
    test_wb_self_not_eligible_shapes();
    test_wb_vote_edges();
    test_wb_subquorum_tally();
    test_wb_next_index_floor();
    test_wb_commit_term_rule();
    printf("test_raft: all passed\n");
    return 0;
}
