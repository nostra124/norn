/**
 * @file keydir.h
 * @brief Fleet key directory: publish/resolve SSH + GPG public keys (FEAT-031).
 *
 * Each member's public keys live in the cluster KV under well-known keys:
 *   peer/<hex-nodeid>/ssh        → `ssh-ed25519 AAAA…` (fits inline)
 *   peer/<hex-nodeid>/gpg        → manifest {n, len, sha256} (written last)
 *   peer/<hex-nodeid>/gpg/<i>    → armored-GPG chunk i
 *
 * Armored GPG keys (~1–4 KB) exceed the cluster value cap, so they are chunked
 * and gated on a manifest that readers verify (length + SHA-256) before
 * accepting the reassembled key. All functions here are pure given a backend;
 * they call only the backend's get/put. The backend is the same vtable the IPC
 * dispatcher uses (so it can be faked in tests).
 */
#ifndef NORND_KEYDIR_H
#define NORND_KEYDIR_H

#include <stddef.h>
#include "dispatch.h" /* nornd_backend_t, NORND_PUBKEY */

#define NORND_KEYDIR_CHUNK 3072 /* max bytes of armored GPG per chunk value */

/* Key-name builders. Each writes a NUL-terminated key into `out` and returns
 * its length, or -1 if it would not fit. */
int nornd_keydir_ssh_key(const unsigned char id[NORND_PUBKEY], char *out, size_t cap);
int nornd_keydir_gpg_key(const unsigned char id[NORND_PUBKEY], char *out, size_t cap);
int nornd_keydir_gpg_chunk_key(const unsigned char id[NORND_PUBKEY], int idx,
                               char *out, size_t cap);

/* Publish this node's SSH public-key line. Returns 0 / -1. */
int nornd_keydir_put_ssh(const nornd_backend_t *be,
                         const unsigned char id[NORND_PUBKEY], const char *sshline);
/* Resolve a peer's SSH line into `out` (NUL-terminated). Returns length / -1. */
int nornd_keydir_get_ssh(const nornd_backend_t *be,
                         const unsigned char id[NORND_PUBKEY], char *out, size_t cap);

/* Publish a peer's armored GPG key: chunk it, then write the manifest last.
 * Returns 0 / -1 (empty/oversized input or a backend put failure). */
int nornd_keydir_put_gpg(const nornd_backend_t *be,
                         const unsigned char id[NORND_PUBKEY],
                         const unsigned char *armor, size_t len);
/* Resolve and verify a peer's GPG key into `out`. Returns the byte length, or
 * -1 if the manifest/chunks are missing, malformed, or fail verification. */
int nornd_keydir_get_gpg(const nornd_backend_t *be,
                         const unsigned char id[NORND_PUBKEY], unsigned char *out,
                         size_t cap);

#endif /* NORND_KEYDIR_H */
