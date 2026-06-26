/**
 * @file dispatch.h
 * @brief Pure request→response mapping for the nornd IPC server (FEAT-029).
 *
 * The daemon's socket/event-loop is glue; the interesting part — turning a
 * decoded IPC request into a response by calling the cluster KV — is factored
 * here behind a small backend vtable so it can be unit-tested against a fake
 * cluster. `main.c` wires a `nornd_backend_t` whose callbacks forward to
 * `norn_cluster_kv_*`; tests wire a fake.
 */
#ifndef NORND_DISPATCH_H
#define NORND_DISPATCH_H

#include <stddef.h>
#include "ipc.h"

#define NORND_PUBKEY 32

/**
 * Backend the dispatcher drives. Each callback receives the opaque `ctx`.
 * Semantics mirror norn_cluster: writes return 0 when accepted/forwarded and
 * -1 when there is no leader; `get` returns the value length or -1 if absent.
 */
typedef struct {
    void *ctx;
    int (*put)(void *ctx, const unsigned char *k, size_t kl,
               const unsigned char *v, size_t vl);
    int (*del)(void *ctx, const unsigned char *k, size_t kl);
    int (*get)(void *ctx, const unsigned char *k, size_t kl, unsigned char *out,
               size_t cap);
    int (*is_leader)(void *ctx);
    /** Current leader pubkey (NORND_PUBKEY bytes) or NULL if unknown. */
    const unsigned char *(*leader)(void *ctx);
    /** Copy up to `max` member pubkeys into `out`; return count written. */
    int (*members)(void *ctx, unsigned char out[][NORND_PUBKEY], int max);
} nornd_backend_t;

/**
 * Map one decoded request to a response. Never fails: malformed/unsupported
 * requests yield an `ok=0` response with an `err` string. `watch` returns an
 * `ok=1` acknowledgement — the streaming itself is the daemon's job.
 */
void nornd_dispatch(const nornd_backend_t *be, const nornd_ipc_req_t *req,
                    nornd_ipc_resp_t *resp);

#endif /* NORND_DISPATCH_H */
