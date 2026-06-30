/* SPDX-License-Identifier: MIT */
/**
 * @file served_proto.h
 * @brief Node-served KV stream protocol codec (FEAT-033).
 *
 * The third KV surface: a node serves its own key-values directly to a peer
 * that dials it and opens a norn stream. This is the pure wire contract for
 * that stream — a one-line request followed by a one-line status header, after
 * which the value bytes are streamed raw (so arbitrarily large objects never
 * buffer in memory). The streaming/file-backing/dial live in glue; the line
 * codec here is testable in isolation.
 *
 * @code
 * Request : "GET <key>\n" | "CAT <hash-hex>\n" | "LIST <prefix>\n"
 * Status  : "OK <len>\n"  (then <len> bytes follow) | "ERR <message>\n"
 * @endcode
 */
#ifndef NORND_SERVED_PROTO_H
#define NORND_SERVED_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define NORND_SERVED_MAX_ARG 256
#define NORND_SERVED_MAX_ERR 256

typedef enum {
    NORND_SERVED_GET,  /* fetch a mutable value by key            */
    NORND_SERVED_CAT,  /* fetch immutable content by sha256 hash  */
    NORND_SERVED_LIST, /* list mutable keys under a prefix        */
} nornd_served_verb_t;

typedef struct {
    nornd_served_verb_t verb;
    char arg[NORND_SERVED_MAX_ARG];
    size_t arglen;
} nornd_served_req_t;

/* Encode a request line (with trailing '\n') into `out`. Returns length or -1
 * (bad verb / missing-or-oversized arg / won't fit). LIST accepts an empty
 * arg; GET and CAT require one. */
int nornd_served_encode_req(nornd_served_verb_t verb, const char *arg, char *out,
                            size_t cap);

/* Parse one request line (a trailing '\n' is optional). Returns 0 / -1. */
int nornd_served_parse_req(const char *line, size_t len, nornd_served_req_t *req);

/* Encode a status line. ok!=0 → "OK <len>\n"; ok==0 → "ERR <err>\n" (err NULL
 * becomes "error"). Returns length or -1 if it won't fit. */
int nornd_served_encode_status(int ok, uint64_t len, const char *err, char *out,
                               size_t cap);

/* Parse a status line. On success sets *ok; if ok, *len_out; else copies the
 * message into err. Returns 0 / -1. */
int nornd_served_parse_status(const char *line, size_t len, int *ok,
                              uint64_t *len_out, char *err, size_t errcap);

#endif /* NORND_SERVED_PROTO_H */
