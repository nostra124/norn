/* SPDX-License-Identifier: MIT */
/**
 * @file ipc.h
 * @brief norn <-> nornd IPC protocol codec (FEAT-027).
 *
 * Length-prefixed bencode request/response frames over the Unix socket. Pure
 * codec (no sockets): a 4-byte big-endian body length followed by a bencoded
 * dict. Binary-safe. This is nornd application code, not libnorn.
 */
#ifndef NORND_IPC_H
#define NORND_IPC_H

#include <stddef.h>
#include <stdint.h>

#define NORND_IPC_MAX_OP    24    /* verb string (NUL-terminated)           */
#define NORND_IPC_MAX_KEY   256   /* key bytes                              */
#define NORND_IPC_MAX_VAL   4096  /* control values; bulk data streams apart */
#define NORND_IPC_ID_BYTES  32    /* target node pubkey for peer ops        */
#define NORND_IPC_MAX_ITEMS 32    /* response list entries (members/ls)     */
#define NORND_IPC_MAX_ITEM  256   /* bytes per response list entry          */
#define NORND_IPC_MAX_BODY  65536 /* largest bencode body accepted          */

/** A decoded request. `op` is required; other fields are optional. */
typedef struct {
    char op[NORND_IPC_MAX_OP];
    unsigned char key[NORND_IPC_MAX_KEY];
    size_t klen;
    unsigned char val[NORND_IPC_MAX_VAL];
    size_t vlen;
    int has_val;
    unsigned char expect[NORND_IPC_MAX_VAL];
    size_t elen;
    int has_expect;
    unsigned char id[NORND_IPC_ID_BYTES]; /* peer/node id (exactly 32 bytes) */
    int has_id;
    int64_t seq;
    int has_seq;
} nornd_ipc_req_t;

/** A decoded response. */
typedef struct {
    int ok; /* 1 success, 0 error */
    unsigned char val[NORND_IPC_MAX_VAL];
    size_t vlen;
    int has_val;
    char err[256];
    int has_err;
    int n_items;
    struct {
        unsigned char data[NORND_IPC_MAX_ITEM];
        size_t len;
    } items[NORND_IPC_MAX_ITEMS];
} nornd_ipc_resp_t;

/** Read the 4-byte big-endian body-length prefix. Returns the body length, or
 *  -1 if fewer than 4 bytes are available. */
int64_t nornd_ipc_frame_len(const unsigned char *buf, size_t len);

/** Encode a request/response into a length-prefixed frame in `out`. Returns the
 *  total frame size (prefix + body), or -1 on error (bad args / won't fit). */
int nornd_ipc_encode_req(const nornd_ipc_req_t *r, unsigned char *out, size_t cap);
int nornd_ipc_encode_resp(const nornd_ipc_resp_t *r, unsigned char *out, size_t cap);

/** Decode one complete frame from `buf`. Returns 0 and sets `*consumed` to the
 *  frame size on success; -1 on a malformed or incomplete frame. */
int nornd_ipc_decode_req(const unsigned char *buf, size_t len,
                         nornd_ipc_req_t *r, size_t *consumed);
int nornd_ipc_decode_resp(const unsigned char *buf, size_t len,
                          nornd_ipc_resp_t *r, size_t *consumed);

#endif /* NORND_IPC_H */
