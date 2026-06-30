/* SPDX-License-Identifier: MIT */
/* Unit tests for the stream-tunnel engine (FEAT-018), norn_forward.
 *
 * The pump is transport-agnostic, so every path is driven with in-memory fake
 * endpoints — no sockets, no network. Aims for 100% line + branch coverage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "norn_forward.h"

/* A fake endpoint: a read-source (`in`) plus a write-sink (`out`), with knobs to
 * provoke would-block, EOF, errors, partial writes and backpressure. */
typedef struct {
    /* read side */
    const unsigned char *in;
    size_t in_len, in_off;
    int in_eof;            /* return -1 once drained, else 0 (would-block) */
    int read_block_once;   /* return 0 the first time, regardless of data */
    int read_err;          /* return -2 */
    /* write side */
    unsigned char out[256];
    size_t out_len;
    size_t write_cap;      /* accept at most this many bytes per call (0=all) */
    int write_block;       /* return 0 (backpressure) */
    int write_err;         /* return -2 */
    int shutdown_calls;
    int close_calls;
} fake_t;

static int fake_read(void *ctx, unsigned char *buf, size_t cap) {
    fake_t *f = ctx;
    if (f->read_err) return -2;
    if (f->read_block_once) { f->read_block_once = 0; return 0; }
    if (f->in_off >= f->in_len) return f->in_eof ? -1 : 0;
    size_t n = f->in_len - f->in_off;
    if (n > cap) n = cap;
    memcpy(buf, f->in + f->in_off, n);
    f->in_off += n;
    return (int)n;
}

static int fake_write(void *ctx, const unsigned char *buf, size_t len) {
    fake_t *f = ctx;
    if (f->write_err) return -2;
    if (f->write_block) return 0;
    size_t n = len;
    if (f->write_cap && n > f->write_cap) n = f->write_cap;
    if (f->out_len + n > sizeof(f->out)) n = sizeof(f->out) - f->out_len;
    memcpy(f->out + f->out_len, buf, n);
    f->out_len += n;
    return (int)n;
}

static void fake_shutdown(void *ctx) { ((fake_t *)ctx)->shutdown_calls++; }
static void fake_close(void *ctx) { ((fake_t *)ctx)->close_calls++; }

/* Full vtable (with optional callbacks) and a minimal one (without). */
static const norn_forward_io_t IO_FULL = { fake_read, fake_write, fake_shutdown, fake_close };
static const norn_forward_io_t IO_MIN  = { fake_read, fake_write, NULL, NULL };

/* Drive to completion with a sane iteration bound (guards against a pump bug
 * looping forever). */
static norn_pump_status_t run(norn_pump_t *p) {
    for (int i = 0; i < 10000; i++) {
        norn_pump_status_t s = norn_pump_drive(p);
        if (s != NORN_PUMP_ACTIVE) return s;
    }
    assert(0 && "pump did not terminate");
    return NORN_PUMP_ERROR;
}

static void test_oneway(void) {
    fake_t a = {0}, b = {0};
    a.in = (const unsigned char *)"hello"; a.in_len = 5; a.in_eof = 1;
    b.in_eof = 1;   /* B has nothing to send */

    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 0);
    assert(p);
    assert(norn_pump_status(p) == NORN_PUMP_ACTIVE);
    assert(run(p) == NORN_PUMP_DONE);

    assert(b.out_len == 5 && memcmp(b.out, "hello", 5) == 0);
    assert(a.out_len == 0);
    assert(a.shutdown_calls == 1 && b.shutdown_calls == 1);

    size_t ab = 0, ba = 0;
    norn_pump_stats(p, &ab, &ba);
    assert(ab == 5 && ba == 0);

    /* Driving again after DONE is a no-op returning DONE. */
    assert(norn_pump_drive(p) == NORN_PUMP_DONE);

    norn_pump_free(p);
    assert(a.close_calls == 1 && b.close_calls == 1);
}

static void test_bidirectional(void) {
    fake_t a = {0}, b = {0};
    a.in = (const unsigned char *)"ping"; a.in_len = 4; a.in_eof = 1;
    b.in = (const unsigned char *)"pong"; b.in_len = 4; b.in_eof = 1;

    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 64);
    assert(run(p) == NORN_PUMP_DONE);
    assert(b.out_len == 4 && memcmp(b.out, "ping", 4) == 0);
    assert(a.out_len == 4 && memcmp(a.out, "pong", 4) == 0);
    norn_pump_free(p);
}

static void test_backpressure(void) {
    fake_t a = {0}, b = {0};
    a.in = (const unsigned char *)"data"; a.in_len = 4; a.in_eof = 1;
    b.in_eof = 1;
    b.write_block = 1;   /* destination refuses writes initially */

    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 64);
    assert(norn_pump_drive(p) == NORN_PUMP_ACTIVE);   /* buffered, not delivered */
    assert(b.out_len == 0);

    b.write_block = 0;   /* unblock */
    assert(run(p) == NORN_PUMP_DONE);
    assert(b.out_len == 4 && memcmp(b.out, "data", 4) == 0);
    norn_pump_free(p);
}

static void test_partial_write(void) {
    fake_t a = {0}, b = {0};
    a.in = (const unsigned char *)"abcdef"; a.in_len = 6; a.in_eof = 1;
    b.in_eof = 1;
    b.write_cap = 2;   /* accept 2 bytes per call → exercises memmove compaction */

    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 64);
    assert(run(p) == NORN_PUMP_DONE);
    assert(b.out_len == 6 && memcmp(b.out, "abcdef", 6) == 0);
    norn_pump_free(p);
}

static void test_buffer_full(void) {
    fake_t a = {0}, b = {0};
    a.in = (const unsigned char *)"abcdefgh"; a.in_len = 8;   /* not eof yet */
    b.in_eof = 1;
    b.write_block = 1;

    /* Tiny buffer: it fills (fill == cap) while the writer is blocked. */
    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 4);
    assert(norn_pump_drive(p) == NORN_PUMP_ACTIVE);
    assert(b.out_len == 0);

    a.in_eof = 1;        /* now allow the source to end */
    b.write_block = 0;   /* and the sink to drain */
    assert(run(p) == NORN_PUMP_DONE);
    assert(b.out_len == 8 && memcmp(b.out, "abcdefgh", 8) == 0);
    norn_pump_free(p);
}

/* One direction (A->B) finishes on the first drive while the other (B->A) is
 * still backpressured — exercises the `half_done(ab) && half_done(ba)`
 * short-circuit with the first operand true and the second false. */
static void test_one_direction_done_first(void) {
    fake_t a = {0}, b = {0};
    a.in_eof = 1;          /* A has nothing to send → A->B completes at once */
    a.write_block = 1;     /* but A cannot yet receive → B->A is backpressured */
    b.in = (const unsigned char *)"x"; b.in_len = 1; b.in_eof = 1;

    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 64);
    assert(norn_pump_drive(p) == NORN_PUMP_ACTIVE);   /* A->B done, B->A not */
    assert(a.out_len == 0);

    a.write_block = 0;     /* now A can receive */
    assert(run(p) == NORN_PUMP_DONE);
    assert(a.out_len == 1 && a.out[0] == 'x');
    norn_pump_free(p);
}

static void test_would_block_read(void) {
    fake_t a = {0}, b = {0};
    a.in = (const unsigned char *)"hi"; a.in_len = 2; a.in_eof = 1;
    a.read_block_once = 1;   /* first read returns 0 (would-block) */
    b.in_eof = 1;

    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 64);
    /* First drive makes no progress on A→B (read blocked, B empty/eof). */
    assert(norn_pump_drive(p) == NORN_PUMP_ACTIVE);
    assert(run(p) == NORN_PUMP_DONE);
    assert(b.out_len == 2 && memcmp(b.out, "hi", 2) == 0);
    norn_pump_free(p);
}

static void test_read_error_ab(void) {
    fake_t a = {0}, b = {0};
    a.read_err = 1;   /* A→B read fails */
    b.in_eof = 1;
    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 64);
    assert(norn_pump_drive(p) == NORN_PUMP_ERROR);
    assert(norn_pump_status(p) == NORN_PUMP_ERROR);
    /* Drive after ERROR returns ERROR (status != ACTIVE early-out). */
    assert(norn_pump_drive(p) == NORN_PUMP_ERROR);
    norn_pump_free(p);
}

static void test_read_error_ba(void) {
    fake_t a = {0}, b = {0};
    a.in_eof = 1;     /* A→B completes cleanly */
    b.read_err = 1;   /* B→A read fails → exercises the second half's error path */
    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 64);
    assert(run(p) == NORN_PUMP_ERROR);
    norn_pump_free(p);
}

static void test_write_error(void) {
    fake_t a = {0}, b = {0};
    a.in = (const unsigned char *)"x"; a.in_len = 1; a.in_eof = 1;
    b.in_eof = 1;
    b.write_err = 1;   /* A→B write fails */
    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 64);
    assert(run(p) == NORN_PUMP_ERROR);
    norn_pump_free(p);
}

static void test_optional_callbacks(void) {
    /* IO_MIN has NULL shutdown/close → exercises the "no optional cb" branches. */
    fake_t a = {0}, b = {0};
    a.in = (const unsigned char *)"z"; a.in_len = 1; a.in_eof = 1;
    b.in_eof = 1;
    norn_pump_t *p = norn_pump_new(&IO_MIN, &a, &IO_MIN, &b, 0);
    assert(run(p) == NORN_PUMP_DONE);
    assert(b.out_len == 1 && b.out[0] == 'z');
    assert(a.shutdown_calls == 0 && b.shutdown_calls == 0);
    norn_pump_free(p);
    assert(a.close_calls == 0 && b.close_calls == 0);
}

static void test_bufsize_bounds(void) {
    fake_t a = {0}, b = {0};
    a.in_eof = 1; b.in_eof = 1;

    /* bufsize 0 → default; huge → clamped to NORN_PUMP_MAX_BUF. Both must build. */
    norn_pump_t *p0 = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 0);
    assert(p0);
    norn_pump_free(p0);

    norn_pump_t *pbig = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b,
                                      (size_t)NORN_PUMP_MAX_BUF + 4096);
    assert(pbig);
    norn_pump_free(pbig);
}

static void test_stats_and_status_null(void) {
    /* stats tolerates NULL out pointers and a NULL pump. */
    fake_t a = {0}, b = {0};
    a.in = (const unsigned char *)"ab"; a.in_len = 2; a.in_eof = 1;
    b.in_eof = 1;
    norn_pump_t *p = norn_pump_new(&IO_FULL, &a, &IO_FULL, &b, 0);
    assert(run(p) == NORN_PUMP_DONE);
    norn_pump_stats(p, NULL, NULL);   /* both out NULL */
    size_t only = 0;
    norn_pump_stats(p, &only, NULL);  /* b_to_a NULL */
    assert(only == 2);
    norn_pump_free(p);

    norn_pump_stats(NULL, &only, &only);            /* NULL pump: no crash */
    assert(norn_pump_status(NULL) == NORN_PUMP_ERROR);
    assert(norn_pump_drive(NULL) == NORN_PUMP_ERROR);
    norn_pump_free(NULL);
}

static void test_new_null_args(void) {
    norn_forward_io_t bad = { NULL, NULL, NULL, NULL };
    norn_forward_io_t no_write = { fake_read, NULL, NULL, NULL };
    norn_forward_io_t no_read  = { NULL, fake_write, NULL, NULL };
    fake_t a = {0}, b = {0};

    assert(norn_pump_new(NULL, &a, &IO_FULL, &b, 0) == NULL);
    assert(norn_pump_new(&IO_FULL, &a, NULL, &b, 0) == NULL);
    assert(norn_pump_new(&no_read, &a, &IO_FULL, &b, 0) == NULL);
    assert(norn_pump_new(&no_write, &a, &IO_FULL, &b, 0) == NULL);
    assert(norn_pump_new(&IO_FULL, &a, &no_read, &b, 0) == NULL);
    assert(norn_pump_new(&IO_FULL, &a, &no_write, &b, 0) == NULL);
    assert(norn_pump_new(&bad, &a, &IO_FULL, &b, 0) == NULL);
}

int main(void) {
    test_oneway();
    test_bidirectional();
    test_backpressure();
    test_partial_write();
    test_buffer_full();
    test_one_direction_done_first();
    test_would_block_read();
    test_read_error_ab();
    test_read_error_ba();
    test_write_error();
    test_optional_callbacks();
    test_bufsize_bounds();
    test_stats_and_status_null();
    test_new_null_args();
    printf("test_forward: all passed\n");
    return 0;
}
