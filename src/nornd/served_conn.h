/* SPDX-License-Identifier: MIT */
/**
 * @file served_conn.h
 * @brief Serve-side driver for one node-served-KV stream (FEAT-033).
 *
 * A peer dials this node and opens a NORN_SVC_SERVED_KV stream (see
 * served_proto.h for the line protocol). This drives that stream through the
 * poll loop: accumulate the one-line request, resolve it with served.h against
 * the node's backends, write the status line, then stream the body (an inline
 * GET/LIST payload, or a CAT object straight from the file-backed store without
 * buffering it whole). One nornd_serve_conn_t per inbound served stream.
 */
#ifndef NORND_SERVED_CONN_H
#define NORND_SERVED_CONN_H

#include <stdio.h>

#include "norn_session.h"
#include "served.h"
#include "served_proto.h"

typedef enum {
    NORND_SERVE_REQ,    /* reading the request line                 */
    NORND_SERVE_STATUS, /* request resolved; status line to write   */
    NORND_SERVE_BODY,   /* streaming the payload                    */
    NORND_SERVE_DONE,   /* finished (stream may be closed)          */
} nornd_serve_phase_t;

typedef struct {
    norn_stream_t *stream;
    nornd_serve_phase_t phase;
    unsigned char req[NORND_SERVED_MAX_ARG + 8]; /* "VERB arg\n"      */
    size_t reqlen;
    nornd_served_result_t res;
    size_t sent;     /* body bytes written so far                    */
    FILE *catf;      /* open CAT object being streamed, or NULL      */
} nornd_serve_conn_t;

/** Bind a fresh serve-conn to an accepted served-KV stream. */
void nornd_serve_conn_init(nornd_serve_conn_t *c, norn_stream_t *stream);

/**
 * Advance the served exchange using whatever is currently readable/writable on
 * the stream. Call once per poll iteration until it returns 1.
 * @return 1 when the exchange is complete (caller may close the stream), else 0.
 */
int nornd_serve_conn_pump(nornd_serve_conn_t *c, const nornd_served_backend_t *be);

#endif /* NORND_SERVED_CONN_H */
