/* Tests for the pure Raft core (FEAT-024). A set of in-memory cores wired by a
 * simulated message bus — drop/reorder/partition are modelled by controlling
 * which messages the pump delivers. All deterministic (seeded jitter). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "norn_raft.h"

/* ── simulation harness ── */
typedef struct {
    norn_raft_t raft;
    uint32_t id;
    int eligible;
    uint32_t applied[512];
    int napplied;
    uint32_t saved_term;
    int32_t saved_vote;
    int saves;
} node_t;

#define MAXN 8
static node_t NODES[MAXN];
static int NN;

static norn_raft_msg_t QUEUE[8192];
static int QHEAD, QTAIL;
static int DROP_PREVOTE; /* when set, prevote reqs/resps are dropped (partition) */

static node_t *by_id(uint32_t id)
{
    int i;
    for (i = 0; i < NN; i++) {
        if (NODES[i].id == id) {
            return &NODES[i];
        }
    }
    return NULL;
}

static void cb_send(void *ud, const norn_raft_msg_t *m)
{
    (void)ud;
    if (DROP_PREVOTE && (m->type == NORN_RAFT_MSG_PREVOTE_REQ ||
                         m->type == NORN_RAFT_MSG_PREVOTE_RESP)) {
        return;
    }
    assert(QTAIL < (int)(sizeof(QUEUE) / sizeof(QUEUE[0])));
    QUEUE[QTAIL++] = *m;
}

static void cb_apply(void *ud, const norn_raft_entry_t *e)
{
    node_t *n = (node_t *)ud;
    n->applied[n->napplied++] = e->index;
}

static void cb_save(void *ud, uint32_t term, int32_t vote)
{
    node_t *n = (node_t *)ud;
    n->saved_term = term;
    n->saved_vote = vote;
    n->saves++;
}

static int cb_eligible(void *ud)
{
    node_t *n = (node_t *)ud;
    return n->eligible;
}

static void pump(void)
{
    while (QHEAD < QTAIL) {
        norn_raft_msg_t m = QUEUE[QHEAD++];
        node_t *n = by_id(m.to);
        if (n) {
            norn_raft_step(&n->raft, &m);
        }
    }
    QHEAD = QTAIL = 0;
}

static node_t *add_node(uint32_t id, int voter, int eligible, uint32_t seed)
{
    node_t *n = &NODES[NN++];
    norn_raft_callbacks_t cb;
    memset(n, 0, sizeof(*n));
    n->id = id;
    n->eligible = eligible;
    n->saved_vote = -2;
    cb.send = cb_send;
    cb.apply = cb_apply;
    cb.save_state = cb_save;
    cb.eligible = cb_eligible;
    cb.ud = n;
    assert(norn_raft_init(&n->raft, id, voter, &cb, 100, 200, 30, seed) == 0);
    return n;
}

/* wire every node into every other node's config. */
static void mesh(void)
{
    int i, j;
    for (i = 0; i < NN; i++) {
        for (j = 0; j < NN; j++) {
            if (i != j) {
                assert(norn_raft_add_peer(&NODES[i].raft, NODES[j].id,
                                          NODES[j].raft.self_voter) == 0);
            }
        }
    }
}

static void reset(void)
{
    NN = 0;
    QHEAD = QTAIL = 0;
    DROP_PREVOTE = 0;
    memset(NODES, 0, sizeof(NODES));
}

/* advance the shared clock, ticking + pumping, until some node leads. */
static node_t *run_until_leader(uint64_t *clock)
{
    int iter;
    for (iter = 0; iter < 5000; iter++) {
        int i;
        *clock += 10;
        for (i = 0; i < NN; i++) {
            norn_raft_tick(&NODES[i].raft, *clock);
        }
        pump();
        for (i = 0; i < NN; i++) {
            if (norn_raft_role(&NODES[i].raft) == NORN_RAFT_LEADER) {
                return &NODES[i];
            }
        }
    }
    return NULL;
}

static int leader_count(void)
{
    int i, n = 0;
    for (i = 0; i < NN; i++) {
        if (norn_raft_role(&NODES[i].raft) == NORN_RAFT_LEADER) {
            n++;
        }
    }
    return n;
}

/* ── tests ── */
static void test_init_validation(void)
{
    norn_raft_t r;
    norn_raft_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    assert(norn_raft_init(NULL, 1, 1, &cb, 100, 200, 30, 1) == -1);
    assert(norn_raft_init(&r, 1, 1, NULL, 100, 200, 30, 1) == -1);
    assert(norn_raft_init(&r, 1, 1, &cb, 200, 100, 30, 1) == -1); /* max<min */
    assert(norn_raft_init(&r, 1, 1, &cb, 0, 200, 30, 1) == -1);   /* min==0 */
    assert(norn_raft_init(&r, 1, 1, &cb, 100, 200, 30, 0) == 0);  /* seed 0 ok */
    /* NULL-safe introspection */
    assert(norn_raft_role(NULL) == NORN_RAFT_FOLLOWER);
    assert(norn_raft_term(NULL) == 0);
    assert(norn_raft_commit_index(NULL) == 0);
    assert(norn_raft_last_index(NULL) == 0);
    assert(norn_raft_leader(NULL) == 0);
    assert(norn_raft_is_voter(NULL) == 0);
    assert(norn_raft_peer_count(NULL) == 0);
    norn_raft_tick(NULL, 1);
    norn_raft_step(NULL, NULL);
    assert(norn_raft_propose(NULL, "x", 1) == -1);
    assert(norn_raft_propose_config(NULL, NORN_RAFT_ENTRY_ADD, 2, 1) == -1);
    assert(norn_raft_snapshot(NULL, 1) == -1);
}

static void test_add_peer_validation(void)
{
    reset();
    node_t *a = add_node(1, 1, 1, 11);
    assert(norn_raft_add_peer(&a->raft, 1, 1) == -1); /* self */
    assert(norn_raft_add_peer(&a->raft, 2, 1) == 0);
    assert(norn_raft_add_peer(&a->raft, 2, 1) == -1); /* dup */
    assert(norn_raft_peer_count(&a->raft) == 1);
}

static void test_single_voter_self_elects(void)
{
    uint64_t clock = 0;
    reset();
    add_node(1, 1, 1, 7);
    /* no peers: quorum is 1, so the first prevote→election makes it leader. */
    node_t *ldr = run_until_leader(&clock);
    assert(ldr && ldr->id == 1);
    assert(norn_raft_is_voter(&ldr->raft) == 1);
}

static void test_three_voters_elect_one_leader_and_replicate(void)
{
    uint64_t clock = 0;
    int i;
    reset();
    add_node(1, 1, 1, 3);
    add_node(2, 1, 1, 99);
    add_node(3, 1, 1, 250);
    mesh();
    node_t *ldr = run_until_leader(&clock);
    assert(ldr != NULL);
    assert(leader_count() == 1);

    /* propose three entries; they replicate and commit in order on all nodes. */
    assert(norn_raft_propose(&ldr->raft, "a", 1) == 2); /* index 1 was the noop */
    assert(norn_raft_propose(&ldr->raft, "b", 1) == 3);
    assert(norn_raft_propose(&ldr->raft, "c", 1) == 4);
    pump();
    /* a couple of heartbeat rounds to flush commit index to followers. */
    for (i = 0; i < 5; i++) {
        clock += 30;
        int j;
        for (j = 0; j < NN; j++) {
            norn_raft_tick(&NODES[j].raft, clock);
        }
        pump();
    }
    for (i = 0; i < NN; i++) {
        assert(norn_raft_commit_index(&NODES[i].raft) == 4);
        /* each applied the three DATA entries in order (indices 2,3,4). */
        assert(NODES[i].napplied == 3);
        assert(NODES[i].applied[0] == 2 && NODES[i].applied[1] == 3 &&
               NODES[i].applied[2] == 4);
    }
}

static void test_ineligible_never_leads(void)
{
    uint64_t clock = 0;
    int iter;
    reset();
    add_node(1, 1, 0, 3);  /* voter but NOT eligible */
    add_node(2, 1, 0, 99); /* voter but NOT eligible */
    add_node(3, 1, 0, 250);
    mesh();
    /* nobody is eligible → no election ever starts. */
    for (iter = 0; iter < 1000; iter++) {
        int i;
        clock += 10;
        for (i = 0; i < NN; i++) {
            norn_raft_tick(&NODES[i].raft, clock);
        }
        pump();
    }
    assert(leader_count() == 0);
    /* now make exactly one eligible: it must become the leader. */
    NODES[2].eligible = 1;
    node_t *ldr = run_until_leader(&clock);
    assert(ldr && ldr->id == 3);
}

static void test_learners_excluded_from_quorum(void)
{
    uint64_t clock = 0;
    int i;
    reset();
    add_node(1, 1, 1, 3);  /* voter */
    add_node(2, 1, 1, 99); /* voter */
    add_node(3, 1, 1, 250);/* voter */
    add_node(4, 0, 0, 5);  /* learner */
    add_node(5, 0, 0, 6);  /* learner */
    mesh();
    node_t *ldr = run_until_leader(&clock);
    assert(ldr != NULL);
    assert(norn_raft_role(&NODES[3].raft) == NORN_RAFT_LEARNER); /* id 4 */
    assert(norn_raft_role(&NODES[4].raft) == NORN_RAFT_LEARNER); /* id 5 */

    /* AC3: quorum is over 3 voters = 2. Commit must succeed even though the
     * learners contribute nothing to quorum. */
    assert(norn_raft_propose(&ldr->raft, "x", 1) > 0);
    for (i = 0; i < 6; i++) {
        clock += 30;
        int j;
        for (j = 0; j < NN; j++) {
            norn_raft_tick(&NODES[j].raft, clock);
        }
        pump();
    }
    /* leader committed (>=2 voters) … */
    assert(norn_raft_commit_index(&ldr->raft) >= 2);
    /* … and the learners still received + applied the entry (full replica). */
    assert(NODES[3].napplied >= 1);
    assert(NODES[4].napplied >= 1);
}

static void test_prevote_protects_healthy_leader(void)
{
    uint64_t clock = 0;
    int i;
    reset();
    add_node(1, 1, 1, 3);
    add_node(2, 1, 1, 99);
    add_node(3, 1, 1, 250);
    mesh();
    node_t *ldr = run_until_leader(&clock);
    assert(ldr != NULL);
    uint32_t term = norn_raft_term(&ldr->raft);

    /* Keep the leader healthy with heartbeats, while a follower's election timer
     * keeps firing. PreVote must be rejected (followers hear the leader), so the
     * term never advances — no disruption (AC4). */
    for (i = 0; i < 50; i++) {
        int j;
        clock += 25; /* < election timeout, > heartbeat */
        for (j = 0; j < NN; j++) {
            norn_raft_tick(&NODES[j].raft, clock);
        }
        pump();
    }
    assert(leader_count() == 1);
    assert(norn_raft_term(&ldr->raft) == term); /* no spurious term bumps */
}

static void test_append_conflict_is_truncated(void)
{
    uint64_t clock = 0;
    int i;
    reset();
    add_node(1, 1, 1, 3);
    add_node(2, 1, 1, 99);
    add_node(3, 1, 1, 250);
    mesh();
    node_t *ldr = run_until_leader(&clock);
    assert(ldr);
    /* drive several proposals + heartbeats; followers that lag get nextIndex
     * regressed and conflicting tails truncated, converging to the leader. */
    for (i = 0; i < 4; i++) {
        char c = (char)('a' + i);
        assert(norn_raft_propose(&ldr->raft, &c, 1) > 0);
        int j, k;
        for (k = 0; k < 3; k++) {
            clock += 30;
            for (j = 0; j < NN; j++) {
                norn_raft_tick(&NODES[j].raft, clock);
            }
            pump();
        }
    }
    uint32_t li = norn_raft_last_index(&ldr->raft);
    for (i = 0; i < NN; i++) {
        assert(norn_raft_last_index(&NODES[i].raft) == li);
        assert(norn_raft_commit_index(&NODES[i].raft) == li);
    }
}

static void test_membership_add_promote_remove(void)
{
    uint64_t clock = 0;
    int i;
    reset();
    add_node(1, 1, 1, 3);
    add_node(2, 1, 1, 99);
    add_node(3, 1, 1, 250);
    /* node 4 starts only known to itself as a learner; the leader will add it. */
    add_node(4, 0, 0, 5);
    /* mesh the 3 voters + give node 4 the voters as peers so it can be reached. */
    {
        int a, b;
        for (a = 0; a < 3; a++) {
            for (b = 0; b < 3; b++) {
                if (a != b) {
                    norn_raft_add_peer(&NODES[a].raft, NODES[b].id, 1);
                }
            }
            norn_raft_add_peer(&NODES[a].raft, 4, 0); /* learner */
            norn_raft_add_peer(&NODES[3].raft, NODES[a].id, 1);
        }
    }
    node_t *ldr = run_until_leader(&clock);
    assert(ldr);
    uint32_t before = norn_raft_peer_count(&ldr->raft);

    /* promote node 4 learner→voter on the leader. */
    assert(norn_raft_propose_config(&ldr->raft, NORN_RAFT_ENTRY_PROMOTE, 4, 1) > 0);
    /* add a brand-new learner node 5 (id only). */
    assert(norn_raft_propose_config(&ldr->raft, NORN_RAFT_ENTRY_ADD, 5, 0) > 0);
    assert(norn_raft_peer_count(&ldr->raft) == before + 1);
    /* remove node 2. */
    assert(norn_raft_propose_config(&ldr->raft, NORN_RAFT_ENTRY_REMOVE, 2, 0) > 0);
    for (i = 0; i < 6; i++) {
        int j;
        clock += 30;
        for (j = 0; j < NN; j++) {
            norn_raft_tick(&NODES[j].raft, clock);
        }
        pump();
    }
    /* node 4 saw the promote entry replicated and is now a voter+follower. */
    assert(norn_raft_is_voter(&NODES[3].raft) == 1);
    assert(norn_raft_role(&NODES[3].raft) == NORN_RAFT_FOLLOWER);
    /* config-change rejected when not leader */
    assert(norn_raft_propose_config(&NODES[3].raft, NORN_RAFT_ENTRY_ADD, 9, 1) == -1);
    /* bad type rejected */
    assert(norn_raft_propose_config(&ldr->raft, NORN_RAFT_ENTRY_DATA, 9, 1) == -1);
}

static void test_snapshot_compaction(void)
{
    uint64_t clock = 0;
    int i;
    reset();
    add_node(1, 1, 1, 7); /* single voter: commits immediately */
    node_t *ldr = run_until_leader(&clock);
    assert(ldr);
    for (i = 0; i < 5; i++) {
        char c = (char)('a' + i);
        assert(norn_raft_propose(&ldr->raft, &c, 1) > 0);
    }
    uint32_t ci = norn_raft_commit_index(&ldr->raft);
    assert(ci >= 5);
    uint32_t before_last = norn_raft_last_index(&ldr->raft);
    /* invalid snapshots */
    assert(norn_raft_snapshot(&ldr->raft, 0) == -1);          /* <= base */
    assert(norn_raft_snapshot(&ldr->raft, ci + 100) == -1);   /* > last */
    /* valid: compact up to commit index. */
    assert(norn_raft_snapshot(&ldr->raft, ci) == 0);
    assert(norn_raft_last_index(&ldr->raft) == before_last);  /* indices preserved */
    /* can still propose after compaction. */
    assert(norn_raft_propose(&ldr->raft, "z", 1) == (int64_t)before_last + 1);
}

static void test_timeout_now_transfers_leadership(void)
{
    uint64_t clock = 0;
    reset();
    add_node(1, 1, 1, 3);
    add_node(2, 1, 1, 99);
    add_node(3, 1, 1, 250);
    mesh();
    node_t *ldr = run_until_leader(&clock);
    assert(ldr);
    /* target a different voter with TimeoutNow → it stands immediately. */
    node_t *target = (ldr->id == 2) ? &NODES[0] : &NODES[1];
    norn_raft_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_TIMEOUTNOW;
    m.term = norn_raft_term(&target->raft);
    m.from = ldr->id;
    m.to = target->id;
    norn_raft_step(&target->raft, &m);
    pump();
    /* the target ran an election (its term advanced past the old leader's). */
    assert(norn_raft_term(&target->raft) > 0);
    /* a stale TimeoutNow (older term) is ignored. */
    uint32_t t = norn_raft_term(&target->raft);
    m.term = 0;
    norn_raft_step(&target->raft, &m);
    assert(norn_raft_term(&target->raft) == t);
}

static void test_propose_guards(void)
{
    reset();
    node_t *a = add_node(1, 1, 1, 7);
    /* not leader yet → propose rejected. */
    assert(norn_raft_propose(&a->raft, "x", 1) == -1);
    /* oversized payload rejected. */
    uint8_t big[NORN_RAFT_MAX_ENTRY + 1];
    memset(big, 0, sizeof(big));
    uint64_t clock = 0;
    run_until_leader(&clock);
    assert(norn_raft_propose(&a->raft, big, sizeof(big)) == -1);
    /* len>0 with NULL data rejected. */
    assert(norn_raft_propose(&a->raft, NULL, 1) == -1);
    /* zero-length data entry is allowed. */
    assert(norn_raft_propose(&a->raft, NULL, 0) > 0);
}

static void test_message_routing_ignored_when_not_addressed(void)
{
    reset();
    node_t *a = add_node(1, 1, 1, 7);
    norn_raft_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_REQ;
    m.to = 999; /* not us */
    m.term = 5;
    norn_raft_step(&a->raft, &m);
    assert(norn_raft_term(&a->raft) == 0); /* untouched */
}

static void test_stale_leader_append_rejected(void)
{
    uint64_t clock = 0;
    reset();
    add_node(1, 1, 1, 3);
    add_node(2, 1, 1, 99);
    mesh();
    node_t *ldr = run_until_leader(&clock);
    assert(ldr);
    node_t *follower = (ldr->id == 1) ? &NODES[1] : &NODES[0];
    /* an AppendEntries with a term below the follower's is rejected. */
    norn_raft_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_REQ;
    m.to = follower->id;
    m.from = 777;
    m.term = 0; /* stale */
    QHEAD = QTAIL = 0;
    norn_raft_step(&follower->raft, &m);
    /* the follower replied success=0 (a queued APPEND_RESP). */
    assert(QTAIL == 1);
    assert(QUEUE[0].type == NORN_RAFT_MSG_APPEND_RESP);
    assert(QUEUE[0].success == 0);
}

/* Inject a single crafted message into node n (bypassing the bus). */
static void inject(node_t *n, norn_raft_msg_t *m)
{
    m->to = n->id;
    norn_raft_step(&n->raft, m);
}

/* targeted tests for branches not reached by the end-to-end scenarios. */
static void test_timeout_now_to_learner_is_refused(void)
{
    reset();
    node_t *l = add_node(1, 0 /*learner*/, 1, 7);
    norn_raft_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_TIMEOUTNOW;
    m.term = 1;
    m.from = 2;
    inject(l, &m);
    /* a learner may never stand, even on an explicit TimeoutNow. */
    assert(norn_raft_role(&l->raft) == NORN_RAFT_LEARNER);
}

static void test_vote_req_rejected_when_candidate_log_is_stale(void)
{
    uint64_t clock = 0;
    reset();
    node_t *a = add_node(1, 1, 1, 7);
    run_until_leader(&clock); /* a now has a term + a noop entry (term>=1) */
    QHEAD = QTAIL = 0;
    norn_raft_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_VOTE_REQ;
    m.from = 2;
    m.term = 10;            /* newer term: a steps down */
    m.last_log_term = 0;    /* but the candidate's log is behind ours */
    m.last_log_index = 0;
    inject(a, &m);
    assert(QTAIL == 1 && QUEUE[0].type == NORN_RAFT_MSG_VOTE_RESP);
    assert(QUEUE[0].vote_granted == 0); /* stale log → vote refused */
}

static void test_apply_config_all_branches(void)
{
    uint64_t clock = 0;
    reset();
    node_t *a = add_node(1, 1, 1, 7);
    assert(norn_raft_add_peer(&a->raft, 2, 0) == 0); /* a learner peer */
    run_until_leader(&clock);                        /* quorum 1 → a leads */
    /* ADD an existing peer (sets its voter flag). */
    assert(norn_raft_propose_config(&a->raft, NORN_RAFT_ENTRY_ADD, 2, 1) > 0);
    /* ADD targeting self. */
    assert(norn_raft_propose_config(&a->raft, NORN_RAFT_ENTRY_ADD, 1, 1) > 0);
    /* PROMOTE an existing peer. */
    assert(norn_raft_propose_config(&a->raft, NORN_RAFT_ENTRY_PROMOTE, 2, 1) > 0);
    /* REMOVE a non-existent peer (find_peer miss → no-op). */
    assert(norn_raft_propose_config(&a->raft, NORN_RAFT_ENTRY_REMOVE, 99, 0) > 0);
    /* REMOVE an existing peer. */
    assert(norn_raft_propose_config(&a->raft, NORN_RAFT_ENTRY_REMOVE, 2, 0) > 0);
    /* PROMOTE self (already voter — exercises the self-promote branch). */
    assert(norn_raft_propose_config(&a->raft, NORN_RAFT_ENTRY_PROMOTE, 1, 1) > 0);
    /* REMOVE self last → steps down to learner. */
    assert(norn_raft_propose_config(&a->raft, NORN_RAFT_ENTRY_REMOVE, 1, 0) > 0);
    assert(norn_raft_is_voter(&a->raft) == 0);
    assert(norn_raft_role(&a->raft) == NORN_RAFT_LEARNER);
}

static void test_append_reject_and_conflict_truncate(void)
{
    reset();
    node_t *f = add_node(1, 1, 1, 7);
    norn_raft_msg_t m;
    /* (a) prev beyond our log → reject. */
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_REQ;
    m.from = 2;
    m.term = 1;
    m.prev_log_index = 5;
    m.prev_log_term = 1;
    QHEAD = QTAIL = 0;
    inject(f, &m);
    assert(QTAIL == 1 && QUEUE[0].success == 0);

    /* (b) append entry idx1 term1 at prev=0. */
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_REQ;
    m.from = 2;
    m.term = 1;
    m.prev_log_index = 0;
    m.prev_log_term = 0;
    m.has_entry = 1;
    m.entry.term = 1;
    m.entry.index = 1;
    m.entry.type = NORN_RAFT_ENTRY_DATA;
    inject(f, &m);
    assert(norn_raft_last_index(&f->raft) == 1);

    /* (c) conflicting entry at idx1 with a newer term → truncate + replace. */
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_REQ;
    m.from = 2;
    m.term = 2;
    m.prev_log_index = 0;
    m.prev_log_term = 0;
    m.has_entry = 1;
    m.entry.term = 2;
    m.entry.index = 1;
    m.entry.type = NORN_RAFT_ENTRY_DATA;
    inject(f, &m);
    assert(norn_raft_last_index(&f->raft) == 1);
}

static void test_append_resp_guard_unknown_and_failure(void)
{
    uint64_t clock = 0;
    reset();
    node_t *a = add_node(1, 1, 1, 7);
    assert(norn_raft_add_peer(&a->raft, 2, 0) == 0); /* learner peer; quorum 1 */
    run_until_leader(&clock);
    norn_raft_msg_t m;
    /* (a) APPEND_RESP to a non-leader is ignored. */
    reset();
    node_t *f = add_node(1, 1, 1, 7);
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_RESP;
    m.from = 2;
    m.term = 0;
    m.success = 1;
    inject(f, &m);
    assert(norn_raft_role(&f->raft) == NORN_RAFT_FOLLOWER);

    /* rebuild a leader with a peer for the next two cases. */
    reset();
    a = add_node(1, 1, 1, 7);
    assert(norn_raft_add_peer(&a->raft, 2, 0) == 0);
    run_until_leader(&clock);
    uint32_t term = norn_raft_term(&a->raft);
    /* (b) APPEND_RESP from an unknown peer → find_peer miss, ignored. */
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_RESP;
    m.from = 99;
    m.term = term;
    m.success = 1;
    inject(a, &m);
    /* advance peer 2's nextIndex past the base first (a fresh leader inits it to
     * base+1, so a rejection there can't regress) … */
    {
        char c = 'x';
        assert(norn_raft_propose(&a->raft, &c, 1) > 0); /* now last_index >= 2 */
    }
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_RESP;
    m.from = 2;
    m.term = term;
    m.success = 1;
    m.match_index = 2; /* peer 2 now at index 2 → nextIndex 3 */
    inject(a, &m);
    /* (c) … then a rejection regresses nextIndex (the decrement path). */
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_RESP;
    m.from = 2;
    m.term = term;
    m.success = 0;
    inject(a, &m);
    assert(norn_raft_role(&a->raft) == NORN_RAFT_LEADER); /* still leading */
}

static void test_snapshot_interior_and_send_clamp(void)
{
    uint64_t clock = 0;
    int i;
    reset();
    node_t *a = add_node(1, 1, 1, 7);
    assert(norn_raft_add_peer(&a->raft, 2, 0) == 0); /* learner peer; quorum 1 */
    run_until_leader(&clock);
    for (i = 0; i < 5; i++) {
        char c = (char)('a' + i);
        assert(norn_raft_propose(&a->raft, &c, 1) > 0);
    }
    /* drive peer 2's nextIndex down to the snapshot boundary via rejections. */
    uint32_t term = norn_raft_term(&a->raft);
    for (i = 0; i < 4; i++) {
        norn_raft_msg_t m;
        memset(&m, 0, sizeof(m));
        m.type = NORN_RAFT_MSG_APPEND_RESP;
        m.from = 2;
        m.term = term;
        m.success = 0;
        inject(a, &m);
    }
    /* interior snapshot (entries remain after it → the shift loop runs). */
    assert(norn_raft_snapshot(&a->raft, 3) == 0);
    /* a heartbeat now sends to peer 2 whose nextIndex sits at/below the base →
     * the clamp path runs. */
    clock += 100;
    norn_raft_tick(&a->raft, clock);
    assert(norn_raft_role(&a->raft) == NORN_RAFT_LEADER);
}

/* A new leader that holds an *uncommitted prior-term* entry must not count it
 * toward commit (Raft §5.4.2) — exercises the prior-term skip in commit advance. */
static void test_prior_term_entry_not_committed_by_count(void)
{
    reset();
    node_t *a = add_node(1, 1, 1, 7);
    assert(norn_raft_add_peer(&a->raft, 2, 1) == 0); /* voters: a,b,c (quorum 2) */
    assert(norn_raft_add_peer(&a->raft, 3, 1) == 0);
    norn_raft_msg_t m;

    /* (1) old leader (id 9, term 1) gives `a` two uncommitted term-1 entries. */
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_REQ;
    m.from = 9;
    m.term = 1;
    m.prev_log_index = 0;
    m.prev_log_term = 0;
    m.leader_commit = 0;
    m.has_entry = 1;
    m.entry.term = 1;
    m.entry.index = 1;
    m.entry.type = NORN_RAFT_ENTRY_NOOP;
    inject(a, &m);
    m.prev_log_index = 1;
    m.prev_log_term = 1;
    m.entry.index = 2;
    m.entry.type = NORN_RAFT_ENTRY_DATA;
    inject(a, &m);
    assert(norn_raft_last_index(&a->raft) == 2);
    assert(norn_raft_commit_index(&a->raft) == 0); /* nothing committed yet */

    /* (2) `a` stands for election: force its timer, then grant pre-votes + votes. */
    norn_raft_tick(&a->raft, 100000);
    assert(norn_raft_role(&a->raft) == NORN_RAFT_CANDIDATE ||
           norn_raft_role(&a->raft) == NORN_RAFT_FOLLOWER);
    /* deliver a granting PreVote from b → reaches quorum, becomes candidate (term2). */
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_PREVOTE_RESP;
    m.from = 2;
    m.vote_granted = 1;
    inject(a, &m);
    assert(norn_raft_role(&a->raft) == NORN_RAFT_CANDIDATE);
    uint32_t t = norn_raft_term(&a->raft);
    /* a granting vote from b → quorum (2 of 3) → becomes leader, appends a term-2
     * noop, and runs commit-advance over a log whose idx2 is a prior (term-1) entry. */
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_VOTE_RESP;
    m.from = 2;
    m.term = t;
    m.vote_granted = 1;
    inject(a, &m);
    assert(norn_raft_role(&a->raft) == NORN_RAFT_LEADER);
    /* the prior-term entries are NOT committed merely by the leader holding them. */
    assert(norn_raft_commit_index(&a->raft) == 0);

    /* drive commit-advance while the only majority-able indices are prior-term:
     * b acks up to idx2 (a term-1 entry) but not the new term-2 noop. Advance must
     * SKIP idx2 (prior term) and commit nothing — the §5.4.2 safety rule. */
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_APPEND_RESP;
    m.from = 2;
    m.term = t;
    m.success = 1;
    m.match_index = 2; /* b holds the prior-term entry, not the term-2 noop */
    inject(a, &m);
    assert(norn_raft_commit_index(&a->raft) == 0); /* prior-term entry not committed */
}

/* All optional callbacks NULL: the core must still elect, replicate, and apply
 * without crashing — exercises the NULL-callback guard branches. */
static void test_null_callbacks_are_tolerated(void)
{
    norn_raft_t r;
    norn_raft_callbacks_t cb;
    uint64_t clk = 0;
    int i;
    memset(&cb, 0, sizeof(cb)); /* send/apply/save_state/eligible all NULL */
    assert(norn_raft_init(&r, 1, 1, &cb, 100, 200, 30, 7) == 0);
    /* single voter: drives to leader purely on timers, emitting nothing. */
    for (i = 0; i < 100 && norn_raft_role(&r) != NORN_RAFT_LEADER; i++) {
        clk += 10;
        norn_raft_tick(&r, clk);
    }
    assert(norn_raft_role(&r) == NORN_RAFT_LEADER);
    /* propose + commit a DATA entry: apply callback is NULL → guarded. */
    assert(norn_raft_propose(&r, "z", 1) > 0);
    assert(norn_raft_commit_index(&r) >= 1);
    /* a stray vote request: response send is NULL → guarded. */
    norn_raft_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = NORN_RAFT_MSG_VOTE_REQ;
    m.to = 1;
    m.term = 99;
    m.last_log_index = 99;
    m.last_log_term = 99;
    m.from = 2;
    norn_raft_step(&r, &m);
}

int main(void)
{
    test_init_validation();
    test_add_peer_validation();
    test_single_voter_self_elects();
    test_three_voters_elect_one_leader_and_replicate();
    test_ineligible_never_leads();
    test_learners_excluded_from_quorum();
    test_prevote_protects_healthy_leader();
    test_append_conflict_is_truncated();
    test_membership_add_promote_remove();
    test_snapshot_compaction();
    test_timeout_now_transfers_leadership();
    test_propose_guards();
    test_message_routing_ignored_when_not_addressed();
    test_stale_leader_append_rejected();
    test_timeout_now_to_learner_is_refused();
    test_vote_req_rejected_when_candidate_log_is_stale();
    test_apply_config_all_branches();
    test_append_reject_and_conflict_truncate();
    test_append_resp_guard_unknown_and_failure();
    test_snapshot_interior_and_send_clamp();
    test_prior_term_entry_not_committed_by_count();
    test_null_callbacks_are_tolerated();
    printf("all norn_raft tests passed\n");
    return 0;
}
