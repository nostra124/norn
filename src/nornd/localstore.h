/* SPDX-License-Identifier: MIT */
/**
 * @file localstore.h
 * @brief Local served-KV store for `norn node set` (FEAT-node-set).
 *
 * A small in-memory key-value map held by the daemon. `node set <name> <value>`
 * writes here; the served-KV GET/LIST handler reads here. Distinct from the
 * replicated cluster KV: these keys are local to this node, never replicated,
 * and only queryable by a peer that dials this node directly (online+reachable).
 *
 * Pure in-memory: no persistence across daemon restarts. Bounded to
 * LOCALSTORE_MAX_ENTRIES entries; oversized keys/values are rejected.
 */
#ifndef NORND_LOCALSTORE_H
#define NORND_LOCALSTORE_H

#include <stddef.h>

#include "norn_kvstore.h" /* norn_kv_visit_fn */

#define LOCALSTORE_MAX_ENTRIES 256
#define LOCALSTORE_MAX_KEY    256
#define LOCALSTORE_MAX_VAL    4096

typedef struct {
    unsigned char key[LOCALSTORE_MAX_KEY];
    size_t klen;
    unsigned char val[LOCALSTORE_MAX_VAL];
    size_t vlen;
    int used;
} localstore_entry_t;

typedef struct {
    localstore_entry_t entries[LOCALSTORE_MAX_ENTRIES];
} localstore_t;

/** Initialize an empty store (equivalent to zeroing; provided for symmetry). */
void localstore_init(localstore_t *s);

/** Put/replace a key. Returns 0 on success, -1 on overflow/bad args. */
int localstore_put(localstore_t *s, const unsigned char *key, size_t klen,
                   const unsigned char *val, size_t vlen);

/** Copy the value for `key` into `out` (cap bytes); return length or -1. */
int localstore_get(const localstore_t *s, const unsigned char *key, size_t klen,
                   unsigned char *out, size_t cap);

/** Visit each key under `prefix` (empty prefix = all); returns the count
 *  visited. Uses norn_kv_visit_fn so it plugs into the served backend. */
int localstore_list(const localstore_t *s, const unsigned char *prefix,
                    size_t plen, norn_kv_visit_fn fn, void *ud);

#endif /* NORND_LOCALSTORE_H */