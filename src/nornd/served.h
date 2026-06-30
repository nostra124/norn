/* SPDX-License-Identifier: MIT */
/**
 * @file served.h
 * @brief Node-served KV request handler (FEAT-033).
 *
 * Resolves one parsed served-stream request (served_proto.h) against the node's
 * backends and produces what the stream glue must send: a status line plus
 * either an inline payload (GET/LIST — small, control-plane sized) or a file
 * path to stream (CAT — arbitrarily large immutable content from store.h).
 *
 *   GET  <key>   → the mutable value, served from the replicated cluster KV
 *   CAT  <hash>  → immutable content, streamed from the file-backed object store
 *   LIST <prefix>→ newline-joined mutable keys under the prefix
 *
 * Pure given the backend (a fake in tests); the actual stream read/write and
 * the file streaming live in glue.
 */
#ifndef NORND_SERVED_H
#define NORND_SERVED_H

#include <stddef.h>
#include <stdint.h>

#include "ipc.h"           /* NORND_IPC_MAX_VAL */
#include "norn_kvstore.h"  /* norn_kv_visit_fn */
#include "served_proto.h"
#include "store.h"

/** Backends the handler reads. `get`/`list` mirror norn_cluster_kv_*; `store`
 *  backs CAT. Any may be exercised by a fake in tests. */
typedef struct {
    void *ctx;
    /** Copy the value for `key` into `out` (cap bytes); return length or -1. */
    int (*get)(void *ctx, const unsigned char *key, size_t klen,
               unsigned char *out, size_t cap);
    /** Visit each key under `prefix`; return count visited or -1. */
    int (*list)(void *ctx, const unsigned char *prefix, size_t plen,
                norn_kv_visit_fn fn, void *ud);
    const nornd_store_t *store;
} nornd_served_backend_t;

/** Resolution of a served request for the stream glue to act on. */
typedef struct {
    int ok;                       /* 1 → OK <len>; 0 → ERR <err>            */
    uint64_t len;                 /* advertised payload length              */
    char err[NORND_SERVED_MAX_ERR];
    int stream_file;              /* 1 → open and stream `path`; 0 → inline */
    char path[600];               /* CAT backing file (when stream_file)    */
    unsigned char inbuf[NORND_IPC_MAX_VAL]; /* GET/LIST payload (inline)    */
    size_t inlen;
} nornd_served_result_t;

/** Resolve `req` against `be`. Never fails: a miss yields an ERR result. */
void nornd_served_handle(const nornd_served_backend_t *be,
                         const nornd_served_req_t *req,
                         nornd_served_result_t *res);

#endif /* NORND_SERVED_H */
