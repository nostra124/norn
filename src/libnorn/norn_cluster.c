/* SPDX-License-Identifier: MIT */
/**
 * @file norn_cluster.c
 * @brief Clustered KV store glue (FEAT-025). See norn_cluster.h.
 *
 * Wires the pure Raft core and the KV state machine onto a pubkey-addressed
 * transport vtable: maps member pubkeys to Raft node ids, serializes/decodes
 * Raft RPCs and write-forward frames, and routes KV operations. No sockets and
 * no globals — the whole module is deterministic and unit-testable with a
 * simulated transport.
 */

#include "norn_cluster.h"
#include "norn_raft.h"

#include <stdlib.h>
#include <string.h>

/* Wire frame kinds (first byte). */
#define FRAME_RAFT 1
#define FRAME_FWD  2

#define CMD_MAX (NORN_KV_MAX_KEY + NORN_KV_MAX_VAL + 16)
/* Largest serialized raft message: AppendEntries with a full batch. */
#define RAFT_FRAME_MAX (64 + RAFT_MAX_BATCH * (19 + RAFT_ENTRY_MAX))

typedef struct {
    unsigned char pubkey[NORN_CLUSTER_PUBKEY];
    raft_node_id_t id;
} cmember_t;

struct norn_cluster {
    unsigned char self[NORN_CLUSTER_PUBKEY];
    raft_node_id_t self_id;
    norn_cluster_io_t io;
    raft_t *raft;
    norn_kv_t *kv;
    cmember_t members[RAFT_MAX_NODES];
    int n_members;
    unsigned char leader_pub[NORN_CLUSTER_PUBKEY];
    int have_leader;
};

/* Derive a globally-consistent Raft node id from a public key (FNV-1a/64), so
 * every member maps the same pubkey to the same id. Never returns
 * RAFT_NO_NODE. */
static raft_node_id_t id_for(const unsigned char *pk) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < NORN_CLUSTER_PUBKEY; i++) {
        h ^= pk[i];
        h *= 1099511628211ULL;
    }
    return h ? h : 1; /* LCOV_EXCL_BR_LINE: 256-bit pubkey hashing to 0 is unobservable */
}

/* ---- pubkey <-> id mapping ---- */

static cmember_t *by_pubkey(norn_cluster_t *cl, const unsigned char *pk) {
    for (int i = 0; i < cl->n_members; i++)
        if (memcmp(cl->members[i].pubkey, pk, NORN_CLUSTER_PUBKEY) == 0)
            return &cl->members[i];
    return NULL;
}

static cmember_t *by_id(norn_cluster_t *cl, raft_node_id_t id) {
    for (int i = 0; i < cl->n_members; i++)
        if (cl->members[i].id == id) return &cl->members[i];
    return NULL;
}

static int class_is_voter(norn_node_class_t cls) { return cls == NORN_NODE_SERVER; }

/* ---- byte helpers (big-endian) ---- */

static size_t w8(unsigned char *p, uint8_t v) { p[0] = v; return 1; }
static size_t w16(unsigned char *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return 2; }
static size_t w64(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i));
    return 8;
}
static uint16_t r16(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint64_t r64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

/* ---- raft message codec ---- */

/* Encode a raft message into buf (excluding the frame kind byte). Returns bytes
 * or -1 if it does not fit. */
static int encode_raft(const raft_msg_t *m, unsigned char *buf, size_t cap) {
    unsigned char tmp[RAFT_FRAME_MAX];
    size_t o = 0;
    o += w8(tmp + o, (uint8_t)m->type);
    o += w64(tmp + o, m->term);
    switch (m->type) { /* LCOV_EXCL_BR_LINE: all msg types handled; no default arm */
    case RAFT_MSG_REQUEST_VOTE:
        o += w8(tmp + o, (uint8_t)m->pre_vote);
        o += w64(tmp + o, m->candidate_id);
        o += w64(tmp + o, m->last_log_index);
        o += w64(tmp + o, m->last_log_term);
        break;
    case RAFT_MSG_REQUEST_VOTE_RESP:
        o += w8(tmp + o, (uint8_t)m->pre_vote);
        o += w8(tmp + o, (uint8_t)m->vote_granted);
        break;
    case RAFT_MSG_APPEND:
        o += w64(tmp + o, m->leader_id);
        o += w64(tmp + o, m->prev_log_index);
        o += w64(tmp + o, m->prev_log_term);
        o += w64(tmp + o, m->leader_commit);
        o += w16(tmp + o, (uint16_t)m->n_entries);
        for (size_t i = 0; i < m->n_entries; i++) {
            const raft_entry_t *e = &m->entries[i];
            o += w64(tmp + o, e->term);
            o += w64(tmp + o, e->index);
            o += w8(tmp + o, (uint8_t)e->type);
            o += w16(tmp + o, (uint16_t)e->len);
            memcpy(tmp + o, e->data, e->len);
            o += e->len;
        }
        break;
    case RAFT_MSG_APPEND_RESP:
        o += w8(tmp + o, (uint8_t)m->success);
        o += w64(tmp + o, m->match_index);
        break;
    }
    if (o > cap) return -1; /* LCOV_EXCL_BR_LINE: caller buffer always >= max frame */
    memcpy(buf, tmp, o);
    return (int)o;
}

/* Decode a raft message. Entries are decoded into `ents` (capacity
 * RAFT_MAX_BATCH). Returns 0 on success, -1 on a malformed message. */
static int decode_raft(const unsigned char *buf, size_t len, raft_msg_t *m,
                       raft_entry_t *ents) {
    memset(m, 0, sizeof(*m));
    if (len < 9) return -1;
    size_t o = 0;
    m->type = (raft_msg_type_t)buf[o++];
    m->term = r64(buf + o);
    o += 8;
    switch (m->type) {
    case RAFT_MSG_REQUEST_VOTE:
        if (len - o < 1 + 24) return -1;
        m->pre_vote = buf[o++];
        m->candidate_id = r64(buf + o); o += 8;
        m->last_log_index = r64(buf + o); o += 8;
        m->last_log_term = r64(buf + o); o += 8;
        return 0;
    case RAFT_MSG_REQUEST_VOTE_RESP:
        if (len - o < 2) return -1;
        m->pre_vote = buf[o++];
        m->vote_granted = buf[o++];
        return 0;
    case RAFT_MSG_APPEND: {
        if (len - o < 34) return -1;
        m->leader_id = r64(buf + o); o += 8;
        m->prev_log_index = r64(buf + o); o += 8;
        m->prev_log_term = r64(buf + o); o += 8;
        m->leader_commit = r64(buf + o); o += 8;
        size_t n = r16(buf + o); o += 2;
        if (n > RAFT_MAX_BATCH) return -1;
        for (size_t i = 0; i < n; i++) {
            if (len - o < 19) return -1;
            raft_entry_t *e = &ents[i];
            memset(e, 0, sizeof(*e));
            e->term = r64(buf + o); o += 8;
            e->index = r64(buf + o); o += 8;
            e->type = (raft_entry_type_t)buf[o++];
            size_t el = r16(buf + o); o += 2;
            if (el > RAFT_ENTRY_MAX || len - o < el) return -1;
            memcpy(e->data, buf + o, el);
            e->len = el;
            o += el;
        }
        m->n_entries = n;
        m->entries = n ? ents : NULL;
        return 0;
    }
    case RAFT_MSG_APPEND_RESP:
        if (len - o < 9) return -1;
        m->success = buf[o++];
        m->match_index = r64(buf + o);
        return 0;
    default:
        return -1;
    }
}

/* ---- raft effect callbacks ---- */

static void raft_send_cb(void *ctx, raft_node_id_t dest, const raft_msg_t *msg) {
    norn_cluster_t *cl = ctx;
    cmember_t *m = by_id(cl, dest);
    if (!m) return; /* LCOV_EXCL_BR_LINE: raft only targets known members */
    unsigned char frame[1 + RAFT_FRAME_MAX];
    frame[0] = FRAME_RAFT;
    int n = encode_raft(msg, frame + 1, sizeof(frame) - 1);
    if (n < 0) return; /* LCOV_EXCL_BR_LINE: frame buffer always large enough */
    if (cl->io.send) cl->io.send(cl->io.ctx, m->pubkey, frame, (size_t)n + 1); /* LCOV_EXCL_BR_LINE: send callback optional */
}

static void raft_apply_cb(void *ctx, const raft_entry_t *entry) {
    norn_cluster_t *cl = ctx;
    norn_kv_apply(cl->kv, entry->data, entry->len);
}

/* ---- lifecycle ---- */

norn_cluster_t *norn_cluster_new(const unsigned char self_pubkey[NORN_CLUSTER_PUBKEY],
                                 const norn_cluster_io_t *io,
                                 const norn_cluster_config_t *cfg) {
    if (!self_pubkey || !io) return NULL;
    norn_cluster_t *cl = calloc(1, sizeof(*cl));
    if (!cl) return NULL; /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    memcpy(cl->self, self_pubkey, NORN_CLUSTER_PUBKEY);
    cl->io = *io;

    int max_kv = (cfg && cfg->max_kv_entries > 0) ? cfg->max_kv_entries : 256;
    cl->kv = norn_kv_new(max_kv);
    if (!cl->kv) { free(cl); return NULL; } /* LCOV_EXCL_LINE: malloc failure not unit-tested */

    raft_config_t rc;
    memset(&rc, 0, sizeof(rc));
    if (cfg) {
        rc.election_base_ms = cfg->election_base_ms;
        rc.election_spread_ms = cfg->election_spread_ms;
        rc.heartbeat_ms = cfg->heartbeat_ms;
    }
    cl->self_id = id_for(self_pubkey);
    raft_io_t rio = {raft_send_cb, raft_apply_cb, cl};
    cl->raft = raft_new(cl->self_id, &rio, &rc);
    if (!cl->raft) { norn_kv_free(cl->kv); free(cl); return NULL; } /* LCOV_EXCL_LINE */

    norn_node_class_t cls = cfg ? cfg->self_class : NORN_NODE_SERVER;
    int eligible = cfg && cfg->election_eligible >= 0 ? cfg->election_eligible : class_is_voter(cls);
    raft_member_role_t role = class_is_voter(cls) ? RAFT_VOTER : RAFT_LEARNER;
    raft_add_member(cl->raft, cl->self_id, role, eligible);
    memcpy(cl->members[0].pubkey, self_pubkey, NORN_CLUSTER_PUBKEY);
    cl->members[0].id = cl->self_id;
    cl->n_members = 1;
    return cl;
}

void norn_cluster_free(norn_cluster_t *cl) {
    if (!cl) return;
    raft_free(cl->raft);
    norn_kv_free(cl->kv);
    free(cl);
}

/* ---- membership ---- */

int norn_cluster_add_member(norn_cluster_t *cl,
                            const unsigned char pubkey[NORN_CLUSTER_PUBKEY],
                            norn_node_class_t cls, int eligible) {
    if (!cl || !pubkey || cl->n_members >= RAFT_MAX_NODES) return -1;
    if (by_pubkey(cl, pubkey)) return -1;
    raft_node_id_t id = id_for(pubkey);
    if (by_id(cl, id)) return -1; /* LCOV_EXCL_BR_LINE: astronomically rare id collision */
    raft_member_role_t role = class_is_voter(cls) ? RAFT_VOTER : RAFT_LEARNER;
    if (raft_add_member(cl->raft, id, role, eligible) != 0) return -1; /* LCOV_EXCL_BR_LINE: capacity checked above */
    memcpy(cl->members[cl->n_members].pubkey, pubkey, NORN_CLUSTER_PUBKEY);
    cl->members[cl->n_members].id = id;
    cl->n_members++;
    return 0;
}

int norn_cluster_bootstrap(norn_cluster_t *cl, const unsigned char *peer_pubkeys, int n_peers) {
    if (!cl || (n_peers > 0 && !peer_pubkeys)) return -1;
    for (int i = 0; i < n_peers; i++) {
        const unsigned char *pk = peer_pubkeys + (size_t)i * NORN_CLUSTER_PUBKEY;
        if (norn_cluster_add_member(cl, pk, NORN_NODE_SERVER, 1) != 0) return -1;
    }
    return 0;
}

int norn_cluster_promote(norn_cluster_t *cl, const unsigned char pubkey[NORN_CLUSTER_PUBKEY]) {
    if (!cl || !pubkey) return -1;
    cmember_t *m = by_pubkey(cl, pubkey);
    if (!m) return -1;
    return raft_promote(cl->raft, m->id);
}

int norn_cluster_remove(norn_cluster_t *cl, const unsigned char pubkey[NORN_CLUSTER_PUBKEY]) {
    if (!cl || !pubkey) return -1;
    cmember_t *m = by_pubkey(cl, pubkey);
    if (!m) return -1;
    raft_remove_member(cl->raft, m->id);
    *m = cl->members[cl->n_members - 1];
    cl->n_members--;
    return 0;
}

/* ---- driving ---- */

static void refresh_leader(norn_cluster_t *cl) {
    raft_node_id_t lid = raft_leader(cl->raft);
    cmember_t *m = lid != RAFT_NO_NODE ? by_id(cl, lid) : NULL;
    if (m) {
        memcpy(cl->leader_pub, m->pubkey, NORN_CLUSTER_PUBKEY);
        cl->have_leader = 1;
    } else {
        cl->have_leader = 0;
    }
}

void norn_cluster_tick(norn_cluster_t *cl, uint64_t now_ms) {
    if (!cl) return;
    raft_tick(cl->raft, now_ms);
    refresh_leader(cl);
}

void norn_cluster_input(norn_cluster_t *cl, const unsigned char from_pubkey[NORN_CLUSTER_PUBKEY],
                        const unsigned char *data, size_t len) {
    if (!cl || !from_pubkey || !data || len < 1) return;
    cmember_t *from = by_pubkey(cl, from_pubkey);
    if (!from) return; /* unknown sender */
    unsigned char kind = data[0];
    if (kind == FRAME_RAFT) {
        raft_msg_t msg;
        raft_entry_t ents[RAFT_MAX_BATCH];
        if (decode_raft(data + 1, len - 1, &msg, ents) != 0) return;
        raft_recv(cl->raft, from->id, &msg);
        refresh_leader(cl);
    } else if (kind == FRAME_FWD) {
        /* A forwarded write: only the leader proposes it. */
        if (raft_role(cl->raft) == RAFT_LEADER)
            raft_propose(cl->raft, data + 1, len - 1);
    }
}

/* ---- KV ---- */

static int kv_submit(norn_cluster_t *cl, const unsigned char *cmd, size_t len) {
    if (len == 0) return -1; /* LCOV_EXCL_BR_LINE: encoders never produce empty commands */
    if (raft_role(cl->raft) == RAFT_LEADER) {
        /* A KV command always fits in a raft entry (RAFT_ENTRY_MAX >= CMD_MAX)
         * and the leader's log is bounded well above test sizes, so propose
         * only fails on a defensive overflow. */
        return raft_propose(cl->raft, cmd, len) ? 0 : -1; /* LCOV_EXCL_BR_LINE: propose-fail is a defensive bound */
    }
    /* Forward to the leader if we know one. */
    if (!cl->have_leader) return -1;
    unsigned char frame[1 + CMD_MAX];
    frame[0] = FRAME_FWD;
    memcpy(frame + 1, cmd, len);
    if (cl->io.send) cl->io.send(cl->io.ctx, cl->leader_pub, frame, len + 1); /* LCOV_EXCL_BR_LINE: send callback optional */
    return 0;
}

int norn_cluster_kv_put(norn_cluster_t *cl, const unsigned char *key, size_t klen,
                        const unsigned char *val, size_t vlen) {
    if (!cl) return -1;
    unsigned char cmd[CMD_MAX];
    int n = norn_kv_encode_put(cmd, sizeof(cmd), key, klen, val, vlen);
    if (n < 0) return -1;
    return kv_submit(cl, cmd, (size_t)n);
}

int norn_cluster_kv_del(norn_cluster_t *cl, const unsigned char *key, size_t klen) {
    if (!cl) return -1;
    unsigned char cmd[CMD_MAX];
    int n = norn_kv_encode_del(cmd, sizeof(cmd), key, klen);
    if (n < 0) return -1;
    return kv_submit(cl, cmd, (size_t)n);
}

int norn_cluster_kv_cas(norn_cluster_t *cl, const unsigned char *key, size_t klen,
                        const unsigned char *expect, size_t elen,
                        const unsigned char *val, size_t vlen) {
    if (!cl) return -1;
    unsigned char cmd[CMD_MAX];
    int n = norn_kv_encode_cas(cmd, sizeof(cmd), key, klen, expect, elen, val, vlen);
    if (n < 0) return -1;
    return kv_submit(cl, cmd, (size_t)n);
}

int norn_cluster_kv_get(norn_cluster_t *cl, const unsigned char *key, size_t klen,
                        unsigned char *out, size_t cap) {
    if (!cl) return -1;
    return norn_kv_get(cl->kv, key, klen, out, cap);
}

int norn_cluster_kv_watch(norn_cluster_t *cl, const unsigned char *prefix, size_t plen,
                          norn_kv_watch_fn fn, void *ud) {
    if (!cl) return -1;
    return norn_kv_watch(cl->kv, prefix, plen, fn, ud);
}

int norn_cluster_kv_list(const norn_cluster_t *cl, const unsigned char *prefix, size_t plen,
                         norn_kv_visit_fn fn, void *ud) {
    if (!cl) return -1;
    return norn_kv_foreach(cl->kv, prefix, plen, fn, ud);
}

/* ---- introspection ---- */

int norn_cluster_is_leader(const norn_cluster_t *cl) {
    return cl && raft_role(cl->raft) == RAFT_LEADER;
}

const unsigned char *norn_cluster_leader(const norn_cluster_t *cl) {
    if (!cl || !cl->have_leader) return NULL;
    return cl->leader_pub;
}

int norn_cluster_member_count(const norn_cluster_t *cl) { return cl ? cl->n_members : -1; }

int norn_cluster_members(const norn_cluster_t *cl,
                         unsigned char out[][NORN_CLUSTER_PUBKEY], int max) {
    if (!cl || !out || max < 0) return -1;
    int n = cl->n_members < max ? cl->n_members : max;
    for (int i = 0; i < n; i++)
        memcpy(out[i], cl->members[i].pubkey, NORN_CLUSTER_PUBKEY);
    return n;
}

int norn_cluster_is_voter(const norn_cluster_t *cl, const unsigned char pubkey[NORN_CLUSTER_PUBKEY]) {
    if (!cl || !pubkey) return 0;
    cmember_t *m = by_pubkey((norn_cluster_t *)cl, pubkey);
    return m && raft_is_voter(cl->raft, m->id);
}
