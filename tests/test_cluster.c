/* Unit tests for the cluster glue (FEAT-025), norn_cluster.
 *
 * Drives a multi-node cluster over a simulated pubkey transport (a message
 * queue with deep-copied frames). Exercises election, replication, learners,
 * write-forwarding and the wire codec end to end — no sockets. Aims for 100%
 * line + branch coverage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "norn_cluster.h"
#include "norn_raft.h" /* for RAFT_MAX_NODES */

#define MAXN 6
#define QCAP 16384
#define PK NORN_CLUSTER_PUBKEY

typedef struct {
    unsigned char pub[PK];
    norn_cluster_t *cl;
    int down;
} cnode_t;

typedef struct {
    unsigned char from[PK], to[PK];
    unsigned char data[2048];
    size_t len;
} frame_t;

static cnode_t g_nodes[MAXN];
static int g_n;
static frame_t g_q[QCAP];
static int g_qhead, g_qcount;
static uint64_t g_clock;

static cnode_t *node_by_pub(const unsigned char *pub) {
    for (int i = 0; i < g_n; i++)
        if (memcmp(g_nodes[i].pub, pub, PK) == 0) return &g_nodes[i];
    return NULL;
}

static void io_send(void *ctx, const unsigned char pubkey[PK], const unsigned char *data, size_t len) {
    cnode_t *self = ctx;
    assert(g_qcount < QCAP);
    assert(len <= sizeof(g_q[0].data));
    frame_t *f = &g_q[(g_qhead + g_qcount) % QCAP];
    g_qcount++;
    memcpy(f->from, self->pub, PK);
    memcpy(f->to, pubkey, PK);
    memcpy(f->data, data, len);
    f->len = len;
}

static void deliver_all(void) {
    while (g_qcount > 0) {
        frame_t f = g_q[g_qhead];
        g_qhead = (g_qhead + 1) % QCAP;
        g_qcount--;
        cnode_t *to = node_by_pub(f.to);
        cnode_t *from = node_by_pub(f.from);
        if (!to || to->down || (from && from->down)) continue;
        norn_cluster_input(to->cl, f.from, f.data, f.len);
    }
}

static void step(void) {
    for (int i = 0; i < g_n; i++)
        if (!g_nodes[i].down) norn_cluster_tick(g_nodes[i].cl, g_clock);
    deliver_all();
    g_clock += 1;
}
static void steps(int n) { for (int i = 0; i < n; i++) step(); }

static void mkpub(unsigned char *pub, int i) {
    memset(pub, 0, PK);
    pub[0] = (unsigned char)(i + 1);
    pub[1] = (unsigned char)(0xA0 + i);
}

/* Build n server nodes that all know each other. */
static void cluster(int n, const norn_node_class_t *classes) {
    memset(g_nodes, 0, sizeof(g_nodes));
    g_qhead = g_qcount = 0;
    g_clock = 0;
    g_n = n;
    for (int i = 0; i < n; i++) mkpub(g_nodes[i].pub, i);
    for (int i = 0; i < n; i++) {
        norn_cluster_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.self_class = classes[i];
        cfg.election_eligible = -1;
        cfg.election_base_ms = 20;
        cfg.election_spread_ms = 60;
        cfg.heartbeat_ms = 5;
        norn_cluster_io_t io = {io_send, &g_nodes[i]};
        g_nodes[i].cl = norn_cluster_new(g_nodes[i].pub, &io, &cfg);
        assert(g_nodes[i].cl);
        /* Every node learns the full membership (self is already a member). */
        for (int j = 0; j < n; j++)
            if (j != i)
                assert(norn_cluster_add_member(g_nodes[i].cl, g_nodes[j].pub, classes[j],
                                               classes[j] == NORN_NODE_SERVER) == 0);
    }
}

static void cluster_free(void) {
    for (int i = 0; i < g_n; i++) norn_cluster_free(g_nodes[i].cl);
}

static cnode_t *leader(void) {
    cnode_t *l = NULL;
    for (int i = 0; i < g_n; i++)
        if (!g_nodes[i].down && norn_cluster_is_leader(g_nodes[i].cl)) {
            assert(!l);
            l = &g_nodes[i];
        }
    return l;
}

static int kv_is(norn_cluster_t *cl, const char *k, const char *v) {
    unsigned char out[256];
    int n = norn_cluster_kv_get(cl, (const unsigned char *)k, strlen(k), out, sizeof(out));
    return n == (int)strlen(v) && memcmp(out, v, n) == 0;
}

static void count_visit(void *ud, const unsigned char *key, size_t klen,
                        const unsigned char *val, size_t vlen) {
    (void)key;
    (void)klen;
    (void)val;
    (void)vlen;
    (*(int *)ud)++;
}

/* ---- tests ---- */

static void test_elect_and_replicate(void) {
    norn_node_class_t cls[] = {NORN_NODE_SERVER, NORN_NODE_SERVER, NORN_NODE_SERVER};
    cluster(3, cls);
    steps(60);
    cnode_t *l = leader();
    assert(l);
    assert(norn_cluster_member_count(l->cl) == 3);
    assert(memcmp(norn_cluster_leader(l->cl), l->pub, PK) == 0);

    assert(norn_cluster_kv_put(l->cl, (const unsigned char *)"k", 1,
                               (const unsigned char *)"v1", 2) == 0);
    steps(20);
    for (int i = 0; i < g_n; i++)
        assert(kv_is(g_nodes[i].cl, "k", "v1"));

    /* prefix list sees the replicated key on every node. */
    int seen = 0;
    assert(norn_cluster_kv_list(g_nodes[1].cl, (const unsigned char *)"k", 1,
                                count_visit, &seen) == 1);
    assert(seen == 1);

    /* delete replicates too */
    assert(norn_cluster_kv_del(l->cl, (const unsigned char *)"k", 1) == 0);
    steps(20);
    assert(norn_cluster_kv_get(g_nodes[1].cl, (const unsigned char *)"k", 1, NULL, 0) == -1);
    cluster_free();
}

static void test_cas(void) {
    /* compare-and-set is the single-owner-claim primitive: the condition is
     * checked on apply, so exactly one of two racing claimants wins. */
    norn_node_class_t cls[] = {NORN_NODE_SERVER, NORN_NODE_SERVER, NORN_NODE_SERVER};
    cluster(3, cls);
    steps(60);
    cnode_t *l = leader();
    assert(l);

    /* claim an absent key (expect empty) → succeeds and replicates. */
    assert(norn_cluster_kv_cas(l->cl, (const unsigned char *)"claim", 5,
                               (const unsigned char *)"", 0,
                               (const unsigned char *)"node-1", 6) == 0);
    steps(20);
    for (int i = 0; i < g_n; i++)
        assert(kv_is(g_nodes[i].cl, "claim", "node-1"));

    /* a second claimant with the wrong expectation (absent) is a no-op. */
    assert(norn_cluster_kv_cas(l->cl, (const unsigned char *)"claim", 5,
                               (const unsigned char *)"", 0,
                               (const unsigned char *)"node-2", 6) == 0);
    steps(20);
    assert(kv_is(g_nodes[1].cl, "claim", "node-1")); /* still node-1 */

    /* the owner can hand off with the matching expectation. */
    assert(norn_cluster_kv_cas(l->cl, (const unsigned char *)"claim", 5,
                               (const unsigned char *)"node-1", 6,
                               (const unsigned char *)"node-2", 6) == 0);
    steps(20);
    assert(kv_is(g_nodes[2].cl, "claim", "node-2"));
    cluster_free();
}

static void test_learner_and_voter_query(void) {
    norn_node_class_t cls[] = {NORN_NODE_SERVER, NORN_NODE_SERVER, NORN_NODE_SERVER,
                               NORN_NODE_MOBILE};
    cluster(4, cls);
    steps(60);
    cnode_t *l = leader();
    assert(l);
    /* mobile node (index 3) is a learner, not a voter. */
    assert(norn_cluster_is_voter(l->cl, g_nodes[0].pub) == 1);
    assert(norn_cluster_is_voter(l->cl, g_nodes[3].pub) == 0);

    assert(norn_cluster_kv_put(l->cl, (const unsigned char *)"x", 1,
                               (const unsigned char *)"y", 1) == 0);
    steps(20);
    assert(kv_is(g_nodes[3].cl, "x", "y")); /* learner replicates */

    /* Promote the mobile node to a voter. */
    assert(norn_cluster_promote(l->cl, g_nodes[3].pub) == 0);
    assert(norn_cluster_is_voter(l->cl, g_nodes[3].pub) == 1);
    cluster_free();
}

static void test_write_forwarding(void) {
    norn_node_class_t cls[] = {NORN_NODE_SERVER, NORN_NODE_SERVER, NORN_NODE_SERVER};
    cluster(3, cls);
    steps(60);
    cnode_t *l = leader();
    assert(l);
    /* Find a follower and write through it — it must forward to the leader. */
    cnode_t *f = NULL;
    for (int i = 0; i < g_n; i++)
        if (&g_nodes[i] != l) { f = &g_nodes[i]; break; }
    assert(f && !norn_cluster_is_leader(f->cl));
    assert(norn_cluster_kv_put(f->cl, (const unsigned char *)"fwd", 3,
                               (const unsigned char *)"ok", 2) == 0);
    steps(30);
    for (int i = 0; i < g_n; i++)
        assert(kv_is(g_nodes[i].cl, "fwd", "ok"));
    cluster_free();
}

static void test_remove_member(void) {
    norn_node_class_t cls[] = {NORN_NODE_SERVER, NORN_NODE_SERVER, NORN_NODE_SERVER};
    cluster(3, cls);
    steps(60);
    cnode_t *l = leader();
    assert(norn_cluster_remove(l->cl, g_nodes[2].pub) == 0);
    assert(norn_cluster_member_count(l->cl) == 2);
    assert(norn_cluster_remove(l->cl, g_nodes[2].pub) == -1); /* already gone */
    cluster_free();
}

static void test_no_leader_put_fails(void) {
    /* A lone server before any election has no leader yet → put is rejected
     * until it elects itself. */
    norn_node_class_t cls[] = {NORN_NODE_SERVER, NORN_NODE_SERVER, NORN_NODE_SERVER};
    cluster(3, cls);
    /* Before any ticks there is no leader; a follower write cannot be routed. */
    assert(norn_cluster_kv_put(g_nodes[1].cl, (const unsigned char *)"k", 1,
                               (const unsigned char *)"v", 1) == -1);
    cluster_free();
}

static void test_bootstrap_helper(void) {
    unsigned char self[PK], peers[2 * PK];
    mkpub(self, 0);
    mkpub(peers, 1);
    mkpub(peers + PK, 2);
    norn_cluster_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.self_class = NORN_NODE_SERVER;
    cfg.election_eligible = -1;
    norn_cluster_io_t io = {io_send, &g_nodes[0]};
    memcpy(g_nodes[0].pub, self, PK);
    g_n = 1;
    g_nodes[0].down = 0;
    norn_cluster_t *cl = norn_cluster_new(self, &io, &cfg);
    assert(cl);
    assert(norn_cluster_bootstrap(cl, peers, 2) == 0);
    assert(norn_cluster_member_count(cl) == 3);
    /* duplicate member rejected */
    assert(norn_cluster_add_member(cl, peers, NORN_NODE_SERVER, 1) == -1);
    norn_cluster_free(cl);
}

static void test_config_defaults_and_eligibility(void) {
    /* NULL cfg → defaults (self treated as a server voter). */
    unsigned char self[PK];
    mkpub(self, 0);
    norn_cluster_io_t io = {io_send, &g_nodes[0]};
    memcpy(g_nodes[0].pub, self, PK);
    g_n = 1;
    norn_cluster_t *cl = norn_cluster_new(self, &io, NULL);
    assert(cl);
    assert(norn_cluster_is_voter(cl, self) == 1);
    for (int i = 0; i < 400; i++) norn_cluster_tick(cl, (uint64_t)i * 10);
    assert(norn_cluster_is_leader(cl)); /* sole server self-elects */
    norn_cluster_free(cl);

    /* Forced eligibility 0 on a server → still a voter, but never leads. */
    norn_cluster_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.self_class = NORN_NODE_SERVER;
    cfg.election_eligible = 0;
    cl = norn_cluster_new(self, &io, &cfg);
    assert(cl);
    for (int i = 0; i < 200; i++) norn_cluster_tick(cl, (uint64_t)i * 10);
    assert(!norn_cluster_is_leader(cl)); /* ineligible → no self-election */
    norn_cluster_free(cl);
}

static int g_watch_hits;
static void on_change(void *ud, norn_kv_event_t ev, const unsigned char *key, size_t klen,
                      const unsigned char *val, size_t vlen) {
    (void)ud; (void)ev; (void)key; (void)klen; (void)val; (void)vlen;
    g_watch_hits++;
}

static void test_watch(void) {
    norn_node_class_t cls[] = {NORN_NODE_SERVER, NORN_NODE_SERVER, NORN_NODE_SERVER};
    cluster(3, cls);
    steps(60);
    cnode_t *l = leader();
    g_watch_hits = 0;
    for (int i = 0; i < g_n; i++)
        assert(norn_cluster_kv_watch(g_nodes[i].cl, (const unsigned char *)"a", 1, on_change, NULL) == 0);
    norn_cluster_kv_put(l->cl, (const unsigned char *)"abc", 3, (const unsigned char *)"1", 1);
    steps(20);
    assert(g_watch_hits == g_n); /* fired once per node on apply */
    cluster_free();
}

static void test_input_robustness(void) {
    norn_node_class_t cls[] = {NORN_NODE_SERVER, NORN_NODE_SERVER, NORN_NODE_SERVER};
    cluster(3, cls);
    steps(60);
    unsigned char *p0 = g_nodes[0].pub;
    norn_cluster_t *c0 = g_nodes[0].cl;

    /* Unknown sender is ignored. */
    unsigned char stranger[PK];
    mkpub(stranger, 99);
    unsigned char frame[4] = {1, 0, 0, 0};
    norn_cluster_input(c0, stranger, frame, sizeof(frame));

    /* Known sender, but a malformed RAFT frame (too short) is dropped. */
    unsigned char short_raft[2] = {1, 0};
    norn_cluster_input(c0, g_nodes[1].pub, short_raft, sizeof(short_raft));

    /* Unknown frame kind is ignored. */
    unsigned char weird[3] = {77, 1, 2};
    norn_cluster_input(c0, g_nodes[1].pub, weird, sizeof(weird));

    /* A FWD frame to a non-leader is ignored (no crash); to the leader it
     * proposes. Build a put command and send it as FWD to the leader. */
    cnode_t *l = leader();
    unsigned char fwd[1 + 64];
    fwd[0] = 2;
    /* encode a tiny PUT inline: [op=1][klen=1]['z'][vlen=1]['9'] */
    unsigned char cmd[6] = {1, 0, 1, 'z', 0, 1};
    unsigned char cmd2[7];
    memcpy(cmd2, cmd, 6); cmd2[6] = '9';
    memcpy(fwd + 1, cmd2, 7);
    norn_cluster_input(l->cl, g_nodes[1].pub, fwd, 8);
    steps(20);
    assert(kv_is(l->cl, "z", "9"));

    (void)p0;
    cluster_free();
}

static void test_null_paths(void) {
    unsigned char pub[PK];
    mkpub(pub, 0);
    norn_cluster_io_t io = {io_send, &g_nodes[0]};
    assert(norn_cluster_new(NULL, &io, NULL) == NULL);
    assert(norn_cluster_new(pub, NULL, NULL) == NULL);

    assert(norn_cluster_add_member(NULL, pub, NORN_NODE_SERVER, 1) == -1);
    assert(norn_cluster_bootstrap(NULL, pub, 1) == -1);
    assert(norn_cluster_promote(NULL, pub) == -1);
    assert(norn_cluster_remove(NULL, pub) == -1);
    assert(norn_cluster_kv_put(NULL, pub, 1, pub, 1) == -1);
    assert(norn_cluster_kv_del(NULL, pub, 1) == -1);
    assert(norn_cluster_kv_cas(NULL, pub, 1, pub, 1, pub, 1) == -1);
    assert(norn_cluster_kv_list(NULL, pub, 1, count_visit, NULL) == -1);
    assert(norn_cluster_kv_get(NULL, pub, 1, NULL, 0) == -1);
    assert(norn_cluster_kv_watch(NULL, pub, 1, on_change, NULL) == -1);
    assert(norn_cluster_is_leader(NULL) == 0);
    assert(norn_cluster_leader(NULL) == NULL);
    assert(norn_cluster_member_count(NULL) == -1);
    assert(norn_cluster_is_voter(NULL, pub) == 0);
    norn_cluster_tick(NULL, 0);
    norn_cluster_input(NULL, pub, pub, 1);
    norn_cluster_free(NULL);

    memcpy(g_nodes[0].pub, pub, PK);
    g_n = 1;
    norn_cluster_t *cl = norn_cluster_new(pub, &io, NULL);
    /* bad-args paths that need a live handle */
    assert(norn_cluster_promote(cl, g_nodes[1].pub) == -1); /* absent (uninit pub) */
    assert(norn_cluster_remove(cl, g_nodes[1].pub) == -1);
    assert(norn_cluster_kv_put(cl, NULL, 0, pub, 1) == -1); /* encode fails */
    assert(norn_cluster_kv_cas(cl, NULL, 0, NULL, 0, pub, 1) == -1); /* encode fails */
    assert(norn_cluster_is_voter(cl, NULL) == 0);
    assert(norn_cluster_leader(cl) == NULL); /* no leader yet */
    norn_cluster_input(cl, pub, NULL, 0);    /* NULL data */
    /* member enumeration */
    unsigned char list[4][PK];
    assert(norn_cluster_members(NULL, list, 4) == -1);
    assert(norn_cluster_members(cl, NULL, 4) == -1);
    assert(norn_cluster_members(cl, list, -1) == -1);
    assert(norn_cluster_members(cl, list, 4) == 1);     /* n_members < max */
    assert(memcmp(list[0], pub, PK) == 0);
    assert(norn_cluster_members(cl, list, 0) == 0);     /* max clamps to 0 */
    norn_cluster_free(cl);
}

/* Feed a crafted RAFT frame (kind byte prepended) from a known member. */
static void feed_raft(norn_cluster_t *cl, const unsigned char *from,
                      const unsigned char *payload, size_t plen) {
    unsigned char frame[2048];
    frame[0] = 1; /* FRAME_RAFT */
    memcpy(frame + 1, payload, plen);
    norn_cluster_input(cl, from, frame, plen + 1);
}

static void test_codec_robustness(void) {
    norn_node_class_t cls[] = {NORN_NODE_SERVER, NORN_NODE_SERVER, NORN_NODE_SERVER};
    cluster(3, cls);
    steps(60);
    norn_cluster_t *c0 = g_nodes[0].cl;
    const unsigned char *from = g_nodes[1].pub;

    /* type byte + 8-byte term = 9 bytes minimum; then per-type truncation. */
    unsigned char p[64];
    memset(p, 0, sizeof(p));
    /* invalid type with full 9 bytes → decode default arm. */
    p[0] = 200;
    feed_raft(c0, from, p, 9);
    /* RequestVote truncated (needs 9 + 25). */
    p[0] = 0; /* RAFT_MSG_REQUEST_VOTE */
    feed_raft(c0, from, p, 9);
    /* RequestVoteResp truncated (needs 9 + 2). */
    p[0] = 1;
    feed_raft(c0, from, p, 9);
    /* Append truncated header (needs 9 + 34). */
    p[0] = 2;
    feed_raft(c0, from, p, 9);
    /* Append with n_entries > RAFT_MAX_BATCH. */
    memset(p, 0, sizeof(p));
    p[0] = 2;
    p[9 + 32] = 0xff; p[9 + 33] = 0xff; /* n_entries huge */
    feed_raft(c0, from, p, 9 + 34);
    /* Append claiming one entry but truncated before the entry header. */
    memset(p, 0, sizeof(p));
    p[0] = 2;
    p[9 + 33] = 1; /* n_entries = 1 */
    feed_raft(c0, from, p, 9 + 34); /* no room for the 19-byte entry header */
    /* Append, one entry, entry length > RAFT_ENTRY_MAX. */
    memset(p, 0, sizeof(p));
    p[0] = 2;
    p[9 + 33] = 1;
    /* entry header is 19 bytes; el field at offset 9+34+17 */
    p[9 + 34 + 17] = 0xff; p[9 + 34 + 18] = 0xff; /* el huge */
    feed_raft(c0, from, p, 9 + 34 + 19);
    /* Append, one entry, valid el but the value bytes run past the frame. */
    memset(p, 0, sizeof(p));
    p[0] = 2;
    p[9 + 33] = 1;                 /* n_entries = 1 */
    p[9 + 34 + 17] = 0;            /* el high byte */
    p[9 + 34 + 18] = 100;          /* el = 100, but only 10 data bytes follow */
    feed_raft(c0, from, p, 9 + 34 + 19 + 10);
    /* AppendResp truncated (needs 9 + 9). */
    memset(p, 0, sizeof(p));
    p[0] = 3;
    feed_raft(c0, from, p, 9);

    /* A FWD frame delivered to a follower is ignored (role != leader). */
    cnode_t *follower = NULL;
    for (int i = 0; i < g_n; i++)
        if (!norn_cluster_is_leader(g_nodes[i].cl)) { follower = &g_nodes[i]; break; }
    assert(follower);
    unsigned char fwd[8] = {2, 1, 0, 1, 'q', 0, 1, '7'};
    norn_cluster_input(follower->cl, g_nodes[(follower == &g_nodes[0]) ? 1 : 0].pub, fwd, 8);

    cluster_free();
}

static void test_arg_corners(void) {
    unsigned char pub[PK], other[PK];
    mkpub(pub, 0);
    mkpub(other, 1);
    norn_cluster_io_t io = {io_send, &g_nodes[0]};
    memcpy(g_nodes[0].pub, pub, PK);
    g_n = 1;

    /* cfg with an explicit KV capacity (max_kv_entries > 0 branch). */
    norn_cluster_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.self_class = NORN_NODE_SERVER;
    cfg.election_eligible = -1;
    cfg.max_kv_entries = 32;
    norn_cluster_t *cl = norn_cluster_new(pub, &io, &cfg);
    assert(cl);

    /* add_member NULL pubkey. */
    assert(norn_cluster_add_member(cl, NULL, NORN_NODE_SERVER, 1) == -1);
    /* bootstrap NULL peers with n>0. */
    assert(norn_cluster_bootstrap(cl, NULL, 1) == -1);
    /* bootstrap with a duplicate (self) → add fails. */
    assert(norn_cluster_bootstrap(cl, pub, 1) == -1);
    /* is_voter for an unknown pubkey. */
    assert(norn_cluster_is_voter(cl, other) == 0);
    /* input with NULL from / unknown frame already covered; here len<1. */
    norn_cluster_input(cl, pub, pub, 0);
    norn_cluster_input(cl, NULL, pub, 1);          /* NULL from-pubkey */
    /* bootstrap with n_peers == 0 is a no-op success. */
    assert(norn_cluster_bootstrap(cl, pub, 0) == 0);
    /* promote/remove with NULL pubkey. */
    assert(norn_cluster_promote(cl, NULL) == -1);
    assert(norn_cluster_remove(cl, NULL) == -1);
    /* kv_del with bad args → encode fails. */
    assert(norn_cluster_kv_del(cl, NULL, 0) == -1);

    /* Fill the member table to capacity, then overflow. */
    int added = 1;
    for (int i = 1; added < RAFT_MAX_NODES; i++, added++) {
        unsigned char pk[PK];
        memset(pk, 0, PK);
        pk[0] = (unsigned char)i;
        pk[1] = (unsigned char)(0x40 + i);
        assert(norn_cluster_add_member(cl, pk, NORN_NODE_LAPTOP, 0) == 0);
    }
    unsigned char extra[PK];
    memset(extra, 0xCC, PK);
    assert(norn_cluster_add_member(cl, extra, NORN_NODE_SERVER, 1) == -1); /* full */
    norn_cluster_free(cl);
}

int main(void) {
    test_elect_and_replicate();
    test_cas();
    test_learner_and_voter_query();
    test_write_forwarding();
    test_remove_member();
    test_no_leader_put_fails();
    test_bootstrap_helper();
    test_config_defaults_and_eligibility();
    test_watch();
    test_input_robustness();
    test_null_paths();
    test_codec_robustness();
    test_arg_corners();
    printf("test_cluster: all passed\n");
    return 0;
}
