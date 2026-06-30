/* SPDX-License-Identifier: MIT */
/**
 * @file served_conn.c
 * @brief Serve-side driver for node-served-KV streams (FEAT-033). See header.
 */

#include "served_conn.h"

#include <string.h>

void nornd_serve_conn_init(nornd_serve_conn_t *c, norn_stream_t *stream) {
    memset(c, 0, sizeof(*c));
    c->stream = stream;
    c->phase = NORND_SERVE_REQ;
}

/* Write up to `len` bytes; returns bytes written (0 if the stream is full). */
static int try_write(norn_stream_t *s, const unsigned char *p, size_t len) {
    if (len == 0) return 0;
    int w = norn_stream_write(s, p, len);
    return w > 0 ? w : 0;
}

int nornd_serve_conn_pump(nornd_serve_conn_t *c, const nornd_served_backend_t *be) {
    if (c->phase == NORND_SERVE_REQ) {
        /* Accumulate the request line until newline (bounded). */
        while (c->reqlen < sizeof(c->req)) {
            unsigned char b;
            if (norn_stream_read(c->stream, &b, 1) != 1) break;
            c->req[c->reqlen++] = b;
            if (b == '\n') break;
        }
        if (c->reqlen == 0 || c->req[c->reqlen - 1] != '\n') {
            if (c->reqlen >= sizeof(c->req)) c->phase = NORND_SERVE_DONE; /* overflow */
            return c->phase == NORND_SERVE_DONE;
        }
        nornd_served_req_t req;
        if (nornd_served_parse_req((const char *)c->req, c->reqlen, &req) != 0) {
            memset(&c->res, 0, sizeof(c->res));
            c->res.ok = 0;
            strcpy(c->res.err, "bad request");
        } else {
            nornd_served_handle(be, &req, &c->res);
            if (c->res.ok && c->res.stream_file) {
                c->catf = fopen(c->res.path, "rb");
                if (!c->catf) { /* LCOV_EXCL_BR_LINE: stat passed just before */
                    c->res.ok = 0;                       /* LCOV_EXCL_LINE */
                    strcpy(c->res.err, "open failed");   /* LCOV_EXCL_LINE */
                    c->res.stream_file = 0;              /* LCOV_EXCL_LINE */
                }
            }
        }
        c->phase = NORND_SERVE_STATUS;
    }

    if (c->phase == NORND_SERVE_STATUS) {
        char line[NORND_SERVED_MAX_ERR + 32];
        int n = nornd_served_encode_status(c->res.ok, c->res.len,
                                           c->res.ok ? NULL : c->res.err, line,
                                           sizeof(line));
        if (n < 0) { c->phase = NORND_SERVE_DONE; return 1; } /* LCOV_EXCL_LINE */
        if (try_write(c->stream, (const unsigned char *)line, (size_t)n) != n)
            return 0; /* retry next pump */
        c->phase = (c->res.ok && c->res.len > 0) ? NORND_SERVE_BODY
                                                 : NORND_SERVE_DONE;
    }

    if (c->phase == NORND_SERVE_BODY) {
        if (c->res.stream_file) {
            /* Stream the CAT object straight from disk in bounded chunks. */
            unsigned char buf[1024];
            while (c->sent < c->res.len) {
                size_t want = c->res.len - c->sent;
                if (want > sizeof(buf)) want = sizeof(buf);
                size_t got = fread(buf, 1, want, c->catf);
                if (got == 0) { c->phase = NORND_SERVE_DONE; break; } /* LCOV_EXCL_LINE */
                int w = try_write(c->stream, buf, got);
                if (w <= 0) return 0; /* stream full — resume next pump */
                c->sent += (size_t)w;
                if ((size_t)w < got) { /* partial: rewind unsent bytes */
                    fseek(c->catf, (long)w - (long)got, SEEK_CUR);
                    return 0;
                }
            }
        } else {
            /* Inline GET/LIST payload. */
            while (c->sent < c->res.inlen) {
                int w = try_write(c->stream, c->res.inbuf + c->sent,
                                  c->res.inlen - c->sent);
                if (w <= 0) return 0;
                c->sent += (size_t)w;
            }
        }
        if (c->sent >= c->res.len) c->phase = NORND_SERVE_DONE;
    }

    if (c->phase == NORND_SERVE_DONE && c->catf) {
        fclose(c->catf);
        c->catf = NULL;
    }
    return c->phase == NORND_SERVE_DONE;
}
