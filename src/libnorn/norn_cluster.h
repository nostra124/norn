/**
 * @file norn_cluster.h
 * @brief Clustered key-value store — class-aware Raft over a pubkey transport
 *        (FEAT-025).
 *
 * Binds the pure Raft core (FEAT-024) and the replicated KV state machine
 * (FEAT-026) into a usable cluster: members are addressed by **public key**,
 * RPCs are serialized to bytes and handed to a caller-supplied transport
 * (`norn_cluster_io_t`), and class-aware membership (servers vote/lead, edge
 * devices are learners) is derived from each member's node class.
 *
 * The transport is a vtable rather than a hard dependency on norn sessions, so
 * the whole cluster is deterministic and unit-testable with a simulated
 * transport. A norn-session adapter (dial/listen → send/input) is a thin layer
 * on top: feed inbound bytes to norn_cluster_input() and implement
 * norn_cluster_io_t::send with norn streams.
 */

#ifndef NORN_CLUSTER_H
#define NORN_CLUSTER_H

#include <stddef.h>
#include <stdint.h>
#include "norn_kvstore.h"

/** Public-key length used to address members. */
#define NORN_CLUSTER_PUBKEY 32

/** Member availability class. Drives the default voter/learner role. */
typedef enum {
    NORN_NODE_MOBILE,
    NORN_NODE_LAPTOP,
    NORN_NODE_WORKSTATION,
    NORN_NODE_SERVER,
} norn_node_class_t;

/** Transport: how the cluster sends a wire frame to a member by public key. */
typedef struct {
    void (*send)(void *ctx, const unsigned char pubkey[NORN_CLUSTER_PUBKEY],
                 const unsigned char *data, size_t len);
    void *ctx;
} norn_cluster_io_t;

/** Configuration. */
typedef struct {
    norn_node_class_t self_class; /**< our class (default role/eligibility) */
    uint32_t uptime_score;        /**< app-supplied "proven uptime" signal (reserved) */
    int election_eligible;        /**< -1 = derive from class, else force 0/1 */
    int max_kv_entries;           /**< KV capacity (0 → default 256) */
    uint32_t election_base_ms;    /**< 0 → Raft default */
    uint32_t election_spread_ms;  /**< 0 → Raft default */
    uint32_t heartbeat_ms;        /**< 0 → Raft default */
} norn_cluster_config_t;

typedef struct norn_cluster norn_cluster_t;

/* === Lifecycle === */

/** Create a cluster node identified by `self_pubkey`. `io` is copied; `cfg` may
 *  be NULL. The node is added as its own first member (per its class). Returns
 *  NULL on bad args / allocation failure. */
norn_cluster_t *norn_cluster_new(const unsigned char self_pubkey[NORN_CLUSTER_PUBKEY],
                                 const norn_cluster_io_t *io,
                                 const norn_cluster_config_t *cfg);
void norn_cluster_free(norn_cluster_t *cl);

/* === Membership === */

/** Add a member with an explicit class. SERVER → voting member; other classes
 *  → learner (unless its eligibility is forced). Returns 0 on success, -1 on
 *  error (full, duplicate). */
int norn_cluster_add_member(norn_cluster_t *cl,
                            const unsigned char pubkey[NORN_CLUSTER_PUBKEY],
                            norn_node_class_t cls, int eligible);
/** Form a cluster from a set of server peers (all added as voters, plus self).
 *  Convenience over repeated add_member. */
int norn_cluster_bootstrap(norn_cluster_t *cl,
                           const unsigned char *peer_pubkeys, int n_peers);
/** Promote a learner to a voting member. Returns 0 / -1. */
int norn_cluster_promote(norn_cluster_t *cl,
                         const unsigned char pubkey[NORN_CLUSTER_PUBKEY]);
/** Remove a member. Returns 0 / -1. */
int norn_cluster_remove(norn_cluster_t *cl,
                        const unsigned char pubkey[NORN_CLUSTER_PUBKEY]);

/* === Driving === */

/** Advance time (drives Raft election/heartbeat timers and applies entries). */
void norn_cluster_tick(norn_cluster_t *cl, uint64_t now_ms);
/** Feed an inbound wire frame received from `from_pubkey`. */
void norn_cluster_input(norn_cluster_t *cl,
                        const unsigned char from_pubkey[NORN_CLUSTER_PUBKEY],
                        const unsigned char *data, size_t len);

/* === Key-value API === */

/** Propose a PUT. On the leader it is appended directly; on a follower/learner
 *  it is forwarded to the leader. Returns 0 if accepted (locally proposed or
 *  forwarded), -1 if there is no known leader yet or on error. */
int norn_cluster_kv_put(norn_cluster_t *cl, const unsigned char *key, size_t klen,
                        const unsigned char *val, size_t vlen);
/** Propose a DEL (same routing as put). */
int norn_cluster_kv_del(norn_cluster_t *cl, const unsigned char *key, size_t klen);
/** Propose a compare-and-set: set `key` to `val` iff its current value equals
 *  `expect` (an absent key matches an empty `expect`). Same leader-routing as
 *  put. Returns 0 if accepted (proposed/forwarded), -1 on error/no leader. The
 *  conditional check is evaluated on apply by every replica, so the outcome is
 *  linearizable — the basis for single-owner claims. */
int norn_cluster_kv_cas(norn_cluster_t *cl, const unsigned char *key, size_t klen,
                        const unsigned char *expect, size_t elen,
                        const unsigned char *val, size_t vlen);
/** Local read of the replicated map. Returns value length or -1. */
int norn_cluster_kv_get(norn_cluster_t *cl, const unsigned char *key, size_t klen,
                        unsigned char *out, size_t cap);
/** Register a prefix watch on applied changes. */
int norn_cluster_kv_watch(norn_cluster_t *cl, const unsigned char *prefix, size_t plen,
                          norn_kv_watch_fn fn, void *ud);
/** Enumerate replicated keys under `prefix` (empty matches all) — see
 *  norn_kv_scan. Returns the match count, or -1 on bad args. */
int norn_cluster_kv_scan(norn_cluster_t *cl, const unsigned char *prefix, size_t plen,
                         norn_kv_scan_fn fn, void *ud);

/* === Introspection === */

/** 1 if this node is the current leader. */
int norn_cluster_is_leader(const norn_cluster_t *cl);
/** The current leader's public key, or NULL if unknown. */
const unsigned char *norn_cluster_leader(const norn_cluster_t *cl);
/** Number of members (voters + learners). */
int norn_cluster_member_count(const norn_cluster_t *cl);
/** Copy up to `max` member pubkeys into `out`; returns the number written, or
 *  -1 on bad arguments. */
int norn_cluster_members(const norn_cluster_t *cl,
                         unsigned char out[][NORN_CLUSTER_PUBKEY], int max);
/** 1 if `pubkey` is a voting member. */
int norn_cluster_is_voter(const norn_cluster_t *cl,
                          const unsigned char pubkey[NORN_CLUSTER_PUBKEY]);

#endif /* NORN_CLUSTER_H */
