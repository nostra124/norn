/* SPDX-License-Identifier: MIT */
/**
 * @file norn_forward.c
 * @brief Generic stream-tunnel engine (FEAT-018). See norn_forward.h.
 *
 * Pure, transport-agnostic byte pump: no syscalls, no network, no globals — so
 * it is exercised to full line/branch coverage with in-memory fake endpoints.
 * The socket/session glue lives in the `norn-forward` CLI, not here.
 */

#include "norn_forward.h"

#include <stdlib.h>
#include <string.h>

/* One unidirectional half: read from `src`, buffer, write to `dst`. */
typedef struct {
    const norn_forward_io_t *src;   /* read side  */
    void *src_ctx;
    const norn_forward_io_t *dst;   /* write side */
    void *dst_ctx;
    unsigned char *buf;             /* points into the pump's single block */
    size_t fill;                    /* valid bytes at buf[0 .. fill)        */
    size_t forwarded;               /* total bytes written to dst           */
    int src_eof;                    /* src reported EOF (-1)                 */
    int dst_shut;                   /* dst write side half-closed           */
} half_t;

struct norn_pump {
    norn_forward_io_t a;            /* copies of the caller's vtables */
    norn_forward_io_t b;
    void *a_ctx;
    void *b_ctx;
    size_t bufsize;
    half_t ab;                      /* A -> B */
    half_t ba;                      /* B -> A */
    norn_pump_status_t status;
};

static int half_done(const half_t *h) {
    /* half_drive() establishes the invariant `src_eof && fill==0 => dst_shut`
     * before half_done() is ever evaluated (the half-close step sets dst_shut in
     * the same call), so the dst_shut==0 short-circuit case is unreachable at the
     * call site. */
    return h->src_eof && h->fill == 0 && h->dst_shut;   /* LCOV_EXCL_BR_LINE: dst_shut implied by the two preceding conjuncts */
}

/* Pump one half as far as it will go this iteration.
 * Returns 0 normally, -1 on a fatal endpoint error. */
static int half_drive(half_t *h, size_t cap) {
    for (;;) {
        int progressed = 0;

        /* Fill from the source while there is room and it has not ended. */
        if (!h->src_eof && h->fill < cap) {
            int n = h->src->read(h->src_ctx, h->buf + h->fill, cap - h->fill);
            if (n == -2) return -1;
            if (n == -1) { h->src_eof = 1; progressed = 1; }
            else if (n > 0) { h->fill += (size_t)n; progressed = 1; }
            /* n == 0: would block — no progress from the read. */
        }

        /* Drain toward the destination. */
        if (h->fill > 0) {
            int n = h->dst->write(h->dst_ctx, h->buf, h->fill);
            if (n == -2) return -1;
            if (n > 0) {
                size_t w = (size_t)n;
                if (w < h->fill) memmove(h->buf, h->buf + w, h->fill - w);
                h->fill -= w;
                h->forwarded += w;
                progressed = 1;
            }
            /* n == 0: destination is backpressured. */
        }

        /* Once the source has ended and the buffer is drained, half-close the
         * destination's write side so the peer downstream sees EOF. */
        if (h->src_eof && h->fill == 0 && !h->dst_shut) {
            if (h->dst->shutdown) h->dst->shutdown(h->dst_ctx);
            h->dst_shut = 1;
            progressed = 1;
        }

        if (!progressed) break;
    }
    return 0;
}

norn_pump_t *norn_pump_new(const norn_forward_io_t *a, void *a_ctx,
                           const norn_forward_io_t *b, void *b_ctx,
                           size_t bufsize) {
    if (!a || !b) return NULL;
    if (!a->read || !a->write) return NULL;
    if (!b->read || !b->write) return NULL;

    if (bufsize == 0) bufsize = NORN_PUMP_DEFAULT_BUF;
    if (bufsize > NORN_PUMP_MAX_BUF) bufsize = NORN_PUMP_MAX_BUF;

    /* Single allocation: header followed by the two per-direction buffers. */
    norn_pump_t *p = malloc(sizeof(*p) + 2 * bufsize);
    if (!p) return NULL;   /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    memset(p, 0, sizeof(*p));

    p->a = *a;
    p->b = *b;
    p->a_ctx = a_ctx;
    p->b_ctx = b_ctx;
    p->bufsize = bufsize;
    p->status = NORN_PUMP_ACTIVE;

    unsigned char *bufs = (unsigned char *)p + sizeof(*p);
    p->ab.buf = bufs;
    p->ba.buf = bufs + bufsize;

    /* A -> B */
    p->ab.src = &p->a; p->ab.src_ctx = p->a_ctx;
    p->ab.dst = &p->b; p->ab.dst_ctx = p->b_ctx;
    /* B -> A */
    p->ba.src = &p->b; p->ba.src_ctx = p->b_ctx;
    p->ba.dst = &p->a; p->ba.dst_ctx = p->a_ctx;

    return p;
}

norn_pump_status_t norn_pump_drive(norn_pump_t *p) {
    if (!p) return NORN_PUMP_ERROR;
    if (p->status != NORN_PUMP_ACTIVE) return p->status;

    if (half_drive(&p->ab, p->bufsize) != 0 ||
        half_drive(&p->ba, p->bufsize) != 0) {
        p->status = NORN_PUMP_ERROR;
        return p->status;
    }

    if (half_done(&p->ab) && half_done(&p->ba))
        p->status = NORN_PUMP_DONE;

    return p->status;
}

norn_pump_status_t norn_pump_status(const norn_pump_t *p) {
    if (!p) return NORN_PUMP_ERROR;
    return p->status;
}

void norn_pump_stats(const norn_pump_t *p, size_t *a_to_b, size_t *b_to_a) {
    if (!p) return;
    if (a_to_b) *a_to_b = p->ab.forwarded;
    if (b_to_a) *b_to_a = p->ba.forwarded;
}

void norn_pump_free(norn_pump_t *p) {
    if (!p) return;
    if (p->a.close) p->a.close(p->a_ctx);
    if (p->b.close) p->b.close(p->b_ctx);
    free(p);
}
