/* SPDX-License-Identifier: MIT */
/**
 * @file served_proto.c
 * @brief Node-served KV stream protocol codec (FEAT-033). See served_proto.h.
 */

#include "served_proto.h"

#include <stdio.h>
#include <string.h>

static const char *verb_name(nornd_served_verb_t v) {
    switch (v) {
    case NORND_SERVED_GET:
        return "GET";
    case NORND_SERVED_CAT:
        return "CAT";
    case NORND_SERVED_LIST:
        return "LIST";
    }
    return NULL; /* unknown verb value */
}

int nornd_served_encode_req(nornd_served_verb_t verb, const char *arg, char *out,
                            size_t cap) {
    const char *name = verb_name(verb);
    if (!name) return -1;
    size_t alen = arg ? strlen(arg) : 0;
    if (alen > NORND_SERVED_MAX_ARG) return -1;
    /* GET and CAT need an argument; LIST may omit it. */
    if (alen == 0 && verb != NORND_SERVED_LIST) return -1;
    int n;
    if (alen == 0)
        n = snprintf(out, cap, "%s\n", name);
    else
        n = snprintf(out, cap, "%s %s\n", name, arg);
    return ((size_t)n < cap) ? n : -1;
}

int nornd_served_parse_req(const char *line, size_t len, nornd_served_req_t *req) {
    if (!line || !req) return -1;
    /* Trim a single trailing newline (and a CR before it). */
    if (len > 0 && line[len - 1] == '\n') len--;
    if (len > 0 && line[len - 1] == '\r') len--;
    /* Verb token ends at the first space or end-of-line. */
    size_t v = 0;
    while (v < len && line[v] != ' ') v++;
    const char *arg = NULL;
    size_t alen = 0;
    if (v < len) {
        arg = line + v + 1; /* skip the single separating space */
        alen = len - v - 1;
    }
    if (v == 3 && memcmp(line, "GET", 3) == 0)
        req->verb = NORND_SERVED_GET;
    else if (v == 3 && memcmp(line, "CAT", 3) == 0)
        req->verb = NORND_SERVED_CAT;
    else if (v == 4 && memcmp(line, "LIST", 4) == 0)
        req->verb = NORND_SERVED_LIST;
    else
        return -1;
    if (alen > NORND_SERVED_MAX_ARG) return -1;
    if (alen == 0 && req->verb != NORND_SERVED_LIST) return -1;
    if (alen) memcpy(req->arg, arg, alen);
    req->arglen = alen;
    return 0;
}

int nornd_served_encode_status(int ok, uint64_t len, const char *err, char *out,
                               size_t cap) {
    int n;
    if (ok)
        n = snprintf(out, cap, "OK %llu\n", (unsigned long long)len);
    else
        n = snprintf(out, cap, "ERR %s\n", err ? err : "error");
    return ((size_t)n < cap) ? n : -1;
}

int nornd_served_parse_status(const char *line, size_t len, int *ok,
                              uint64_t *len_out, char *err, size_t errcap) {
    if (!line || !ok) return -1;
    if (len > 0 && line[len - 1] == '\n') len--;
    if (len > 0 && line[len - 1] == '\r') len--;
    if (len >= 3 && memcmp(line, "OK ", 3) == 0) {
        uint64_t v = 0;
        size_t i = 3;
        if (i >= len) return -1; /* "OK " with no number */
        for (; i < len; i++) {
            if (line[i] < '0' || line[i] > '9') return -1;
            v = v * 10 + (uint64_t)(line[i] - '0');
        }
        *ok = 1;
        if (len_out) *len_out = v;
        return 0;
    }
    if (len >= 4 && memcmp(line, "ERR ", 4) == 0) {
        *ok = 0;
        size_t mlen = len - 4;
        if (err && errcap) {
            if (mlen >= errcap) mlen = errcap - 1;
            memcpy(err, line + 4, mlen);
            err[mlen] = '\0';
        }
        return 0;
    }
    return -1;
}
