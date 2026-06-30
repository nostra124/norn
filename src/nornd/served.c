/* SPDX-License-Identifier: MIT */
/**
 * @file served.c
 * @brief Node-served KV request handler (FEAT-033). See served.h.
 */

#include "served.h"

#include <string.h>

static void set_err(nornd_served_result_t *r, const char *msg) {
    r->ok = 0;
    size_t n = strlen(msg);
    if (n >= sizeof(r->err)) n = sizeof(r->err) - 1; /* LCOV_EXCL_BR_LINE */
    memcpy(r->err, msg, n);
    r->err[n] = '\0';
}

/* LIST accumulator: append "<key>\n" per visited key until the inline buffer
 * fills, then silently drop the rest (the OK length reflects what was kept). */
static void list_visit(void *ud, const unsigned char *key, size_t klen,
                       const unsigned char *val, size_t vlen) {
    (void)val;
    (void)vlen;
    nornd_served_result_t *r = ud;
    if (r->inlen + klen + 1 > sizeof(r->inbuf)) return;
    memcpy(r->inbuf + r->inlen, key, klen);
    r->inlen += klen;
    r->inbuf[r->inlen++] = '\n';
}

void nornd_served_handle(const nornd_served_backend_t *be,
                         const nornd_served_req_t *req,
                         nornd_served_result_t *res) {
    memset(res, 0, sizeof(*res));

    if (req->verb == NORND_SERVED_GET) {
        int n = be->get(be->ctx, (const unsigned char *)req->arg, req->arglen,
                        res->inbuf, sizeof(res->inbuf));
        if (n < 0) {
            set_err(res, "not found");
            return;
        }
        res->inlen = (size_t)n;
        res->len = (uint64_t)n;
        res->ok = 1;
        return;
    }

    if (req->verb == NORND_SERVED_CAT) {
        uint64_t len = 0;
        if (nornd_store_stat(be->store, req->arg, req->arglen, res->path,
                             sizeof(res->path), &len) != 0) {
            set_err(res, "not found");
            return;
        }
        res->stream_file = 1;
        res->len = len;
        res->ok = 1;
        return;
    }

    /* LIST: enumerate mutable keys under the prefix into the inline buffer. */
    int n = be->list(be->ctx, (const unsigned char *)req->arg, req->arglen,
                     list_visit, res);
    if (n < 0) {
        set_err(res, "list failed");
        return;
    }
    res->len = (uint64_t)res->inlen;
    res->ok = 1;
}
