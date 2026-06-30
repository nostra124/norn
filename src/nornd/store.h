/* SPDX-License-Identifier: MIT */
/**
 * @file store.h
 * @brief File-backed immutable object store for node-served content (FEAT-033).
 *
 * The `CAT` verb of the node-served KV stream fetches immutable content by its
 * SHA-256 hash. Those objects are kept on disk, content-addressed: each object
 * lives at `<root>/<hex-sha256>` and is written once. Large objects are never
 * buffered whole — the serve glue streams straight from the backing file, so
 * this module only needs to ingest an object, and to resolve a hash to its
 * path + length (a cheap stat) for the streamer.
 *
 * GET/LIST (mutable keys) are served from the replicated cluster KV, not here.
 *
 * Pure given a filesystem root: tests point `root` at a temp directory.
 */
#ifndef NORND_STORE_H
#define NORND_STORE_H

#include <stddef.h>
#include <stdint.h>

/* A SHA-256 hex digest is 64 chars; callers size hash buffers with the NUL. */
#define NORND_STORE_HASH_HEX 64

typedef struct {
    char root[512];
    size_t rootlen;
} nornd_store_t;

/**
 * Bind `s` to object directory `root`, creating it if needed.
 * @return 0 on success, -1 if `root` is too long or cannot be created.
 */
int nornd_store_init(nornd_store_t *s, const char *root);

/**
 * Ingest `data` as an immutable object. Computes its SHA-256, writes
 * `<root>/<hex>` (idempotent — re-storing identical bytes is a no-op), and
 * copies the 64-char lowercase hex digest (NUL-terminated) into `hash_out`.
 * @param hash_out buffer of at least NORND_STORE_HASH_HEX + 1 bytes
 * @return 0 on success, -1 on a write error.
 */
int nornd_store_put(const nornd_store_t *s, const unsigned char *data,
                    size_t len, char *hash_out);

/**
 * Resolve a hex hash to its backing path and byte length without reading the
 * object. `hash`/`hlen` need not be NUL-terminated.
 * @return 0 (found; `path_out`, `len_out` set), -1 (malformed hash or absent).
 */
int nornd_store_stat(const nornd_store_t *s, const char *hash, size_t hlen,
                     char *path_out, size_t pathcap, uint64_t *len_out);

#endif /* NORND_STORE_H */
