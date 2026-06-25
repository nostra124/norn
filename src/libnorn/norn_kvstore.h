/**
 * @file norn_kvstore.h
 * @brief Replicated key-value state machine (FEAT-026).
 *
 * The deterministic state machine behind the clustered KV store: it applies
 * committed Raft log entries (PUT / DEL / CAS) to a bounded in-memory map, and
 * supports prefix watches and snapshots for log compaction. It is pure (no I/O,
 * no globals), so it is unit-testable in isolation and applies identically on
 * every replica.
 *
 * Commands are opaque byte blobs: encode one with norn_kv_encode_* on the
 * proposer, hand it to Raft (`raft_propose`), and on every node Raft's apply
 * callback feeds the committed bytes to norn_kv_apply().
 */

#ifndef NORN_KVSTORE_H
#define NORN_KVSTORE_H

#include <stddef.h>
#include <stdint.h>

/** Max key length (bytes). Holds the key directory's `peer/<64-hex>/gpg/<n>`. */
#define NORN_KV_MAX_KEY 128
/** Max value length (bytes). */
#define NORN_KV_MAX_VAL 256

/** Command opcodes (first byte of an encoded command). */
typedef enum {
    NORN_KV_PUT = 1,
    NORN_KV_DEL = 2,
    NORN_KV_CAS = 3,
} norn_kv_op_t;

/** Watch event kind. */
typedef enum { NORN_KV_EV_PUT, NORN_KV_EV_DEL } norn_kv_event_t;

/**
 * Watch callback. `val`/`vlen` are the new value for PUT (empty for DEL).
 * The pointers are valid only for the duration of the call.
 */
typedef void (*norn_kv_watch_fn)(void *ud, norn_kv_event_t ev,
                                 const unsigned char *key, size_t klen,
                                 const unsigned char *val, size_t vlen);

typedef struct norn_kv norn_kv_t;

/* === Lifecycle === */

/** Create a store holding at most `max_entries` keys. NULL on bad args/OOM. */
norn_kv_t *norn_kv_new(int max_entries);
void norn_kv_free(norn_kv_t *kv);

/* === Command encoding (proposer side) === */

/** Encode a PUT. Returns bytes written, or -1 (bad args / buffer too small). */
int norn_kv_encode_put(unsigned char *buf, size_t cap,
                       const unsigned char *key, size_t klen,
                       const unsigned char *val, size_t vlen);
/** Encode a DEL. */
int norn_kv_encode_del(unsigned char *buf, size_t cap,
                       const unsigned char *key, size_t klen);
/** Encode a compare-and-set: set key to `val` iff its current value equals
 *  `expect` (an absent key matches an empty `expect`). */
int norn_kv_encode_cas(unsigned char *buf, size_t cap,
                       const unsigned char *key, size_t klen,
                       const unsigned char *expect, size_t elen,
                       const unsigned char *val, size_t vlen);

/* === Apply (every replica) === */

/**
 * Apply one committed command.
 * @return 1 if it mutated state (PUT, DEL of an existing key, successful CAS),
 *         0 if it was a no-op (DEL of absent key, failed CAS),
 *         -1 on a malformed command or capacity overflow.
 */
int norn_kv_apply(norn_kv_t *kv, const unsigned char *cmd, size_t len);

/* === Reads === */

/** Copy the value for `key` into `out`. Returns value length, or -1 if absent
 *  (or `out` too small). */
int norn_kv_get(const norn_kv_t *kv, const unsigned char *key, size_t klen,
                unsigned char *out, size_t cap);
/** Number of keys currently stored. */
int norn_kv_count(const norn_kv_t *kv);

/* === Watches === */

/** Register a prefix watch. An empty prefix matches all keys. Returns 0 on
 *  success, -1 on error (table full / bad args). */
int norn_kv_watch(norn_kv_t *kv, const unsigned char *prefix, size_t plen,
                  norn_kv_watch_fn fn, void *ud);

/* === Snapshots (log compaction) === */

/** Serialize the whole store into `buf`. Returns bytes written, or -1 if the
 *  buffer is too small. */
int norn_kv_snapshot(const norn_kv_t *kv, unsigned char *buf, size_t cap);
/** Replace the store's contents from a snapshot. Returns 0 on success, -1 on a
 *  malformed snapshot. Does not fire watches. */
int norn_kv_restore(norn_kv_t *kv, const unsigned char *buf, size_t len);

#endif /* NORN_KVSTORE_H */
