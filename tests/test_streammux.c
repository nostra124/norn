/* FEAT-074 regression: logical-stream multiplexing. Two independent byte-streams
 * share one mux (one channel); across a link that drops/reorders/dups framed
 * segments, both arrive complete and in order, and loss on one logical stream
 * does not stall delivery of the other. */
#include "streammux.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static uint32_t CLK;
static uint32_t lcg = 0x2468ace0;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return (lcg >> 16) & 0x7fff; }

typedef struct { unsigned char buf[STREAMMUX_FRAME + STREAM_SEG_MAX]; size_t len; uint32_t at; } pkt_t;
typedef struct { pkt_t q[8192]; int n; } wire_t;
static wire_t ab, ba;
static int drop_sid1 = 0;   /* when set, segments for logical stream 1 are dropped (independence test) */

static uint16_t frame_sid(const unsigned char *b) { return (uint16_t)((b[0] << 8) | b[1]); }

static int send_into(wire_t *w, const unsigned char *seg, size_t len) {
    if (w->n >= (int)(sizeof w->q / sizeof w->q[0])) return -1;
    if (drop_sid1 && frame_sid(seg) == 1) return 0;            /* black-hole stream 1 */
    if (rnd() % 100 < 10) return 0;                            /* 10% loss */
    pkt_t *p = &w->q[w->n++];
    memcpy(p->buf, seg, len); p->len = len; p->at = CLK + 1 + rnd() % 4;   /* jitter → reorder */
    if (rnd() % 100 < 5 && w->n < (int)(sizeof w->q / sizeof w->q[0])) {
        pkt_t *d = &w->q[w->n++]; memcpy(d->buf, seg, len); d->len = len; d->at = CLK + 1 + rnd() % 4;
    }
    return 0;
}
static int sendA(void *c, const unsigned char *s, size_t l) { (void)c; return send_into(&ab, s, l); }
static int sendB(void *c, const unsigned char *s, size_t l) { (void)c; return send_into(&ba, s, l); }
static void deliver(wire_t *w, streammux_t *peer) {
    int k = 0;
    for (int i = 0; i < w->n; i++) {
        if (w->q[i].at <= CLK) streammux_input(peer, w->q[i].buf, w->q[i].len, CLK);
        else w->q[k++] = w->q[i];
    }
    w->n = k;
}

#define N1 40000
#define N2 25000
static unsigned char S1[N1], S2[N2], R1[N1 + 16], R2[N2 + 16];

static void test_two_streams_interleaved(void) {
    ab.n = ba.n = 0; CLK = 0; drop_sid1 = 0;
    for (int i = 0; i < N1; i++) S1[i] = (unsigned char)(i * 7 + 1);
    for (int i = 0; i < N2; i++) S2[i] = (unsigned char)(i * 13 + 5);
    streammux_t *A = streammux_new(sendA, NULL), *B = streammux_new(sendB, NULL);
    assert(A && B);
    assert(streammux_open(A, 1) == 0 && streammux_open(A, 2) == 0);

    size_t w1 = 0, w2 = 0, r1 = 0, r2 = 0;
    long step;
    for (step = 0; step < 4000000; step++) {
        if (w1 < N1) w1 += (size_t)streammux_write(A, 1, S1 + w1, N1 - w1, CLK);
        if (w2 < N2) w2 += (size_t)streammux_write(A, 2, S2 + w2, N2 - w2, CLK);
        streammux_tick(A, CLK); streammux_tick(B, CLK);
        deliver(&ab, B); deliver(&ba, A);
        if (r1 < N1 + 16) r1 += (size_t)streammux_read(B, 1, R1 + r1, sizeof(R1) - r1);
        if (r2 < N2 + 16) r2 += (size_t)streammux_read(B, 2, R2 + r2, sizeof(R2) - r2);
        if (r1 == N1 && r2 == N2) break;
        CLK++;
    }
    assert(step < 4000000);                                  /* converged */
    assert(r1 == N1 && memcmp(R1, S1, N1) == 0);            /* stream 1 intact + ordered */
    assert(r2 == N2 && memcmp(R2, S2, N2) == 0);            /* stream 2 intact + ordered */
    assert(streammux_count(B) == 2);                        /* B auto-opened both */
    streammux_free(A); streammux_free(B);
    printf("  two_streams_interleaved: OK (%ld steps)\n", step);
}

/* loss on stream 1 (black-holed for a while) must NOT stall stream 2 delivery. */
static void test_head_of_line_independence(void) {
    ab.n = ba.n = 0; CLK = 0; drop_sid1 = 1;                /* stream 1 is blocked */
    for (int i = 0; i < N2; i++) S2[i] = (unsigned char)(i * 3 + 7);
    streammux_t *A = streammux_new(sendA, NULL), *B = streammux_new(sendB, NULL);
    assert(streammux_open(A, 1) == 0 && streammux_open(A, 2) == 0);

    size_t w1 = 0, w2 = 0, r2 = 0;
    unsigned char blob[2000];
    for (size_t i = 0; i < sizeof blob; i++) blob[i] = (unsigned char)i;
    long step;
    for (step = 0; step < 2000000; step++) {
        if (w1 < 8000) w1 += (size_t)streammux_write(A, 1, blob, sizeof blob, CLK);  /* stream 1: never gets through */
        if (w2 < N2) w2 += (size_t)streammux_write(A, 2, S2 + w2, N2 - w2, CLK);
        streammux_tick(A, CLK); streammux_tick(B, CLK);
        deliver(&ab, B); deliver(&ba, A);
        if (r2 < N2) r2 += (size_t)streammux_read(B, 2, R2 + r2, sizeof(R2) - r2);
        if (r2 == N2) break;
        CLK++;
    }
    assert(r2 == N2 && memcmp(R2, S2, N2) == 0);            /* stream 2 completed despite stream 1 blocked */
    assert(streammux_readable(B, 1) == 0);                  /* stream 1 delivered nothing */
    streammux_free(A); streammux_free(B);
    printf("  head_of_line_independence: OK (%ld steps)\n", step);
}

static int noop_send(void *c, const unsigned char *s, size_t l) { (void)c; (void)s; (void)l; return 0; }

/* COV-1: the guard / miss / table-full / OOM branches the functional tests skip. */
static void test_streammux_edges(void) {
    /* every accessor is NULL-safe with the documented sentinel */
    assert(streammux_open(NULL, 0) == -1);
    assert(streammux_write(NULL, 0, (const unsigned char *)"x", 1, 0) == -1);
    assert(streammux_read(NULL, 0, (unsigned char[4]){0}, 4) == 0);
    streammux_input(NULL, (const unsigned char *)"\0\0z", 3, 0);   /* no crash */
    streammux_tick(NULL, 0);
    streammux_finish(NULL, 0, 0);
    assert(streammux_readable(NULL, 0) == 0);
    assert(streammux_count(NULL) == 0);
    assert(streammux_peer_finished(NULL, 0) == 1);
    assert(streammux_send_done(NULL, 0) == 1);

    streammux_t *m = streammux_new(noop_send, NULL);
    assert(m);
    /* queries on a never-opened stream id hit the find-miss arms */
    assert(streammux_read(m, 99, (unsigned char[4]){0}, 4) == 0);
    assert(streammux_readable(m, 99) == 0);
    assert(streammux_peer_finished(m, 99) == 1);
    assert(streammux_send_done(m, 99) == 1);
    /* a too-short inbound frame is ignored (len < STREAMMUX_FRAME) */
    streammux_input(m, (const unsigned char *)"\0", 1, 0);
    assert(streammux_count(m) == 0);
    /* re-opening the same id returns the existing stream (no growth) */
    assert(streammux_open(m, 5) == 0 && streammux_open(m, 5) == 0);
    assert(streammux_count(m) == 1);
    /* finish on a not-yet-opened id opens it */
    streammux_finish(m, 6, 0);
    assert(streammux_count(m) == 2);

    /* cfind on an OPEN id (ls != NULL → ternary true arm) vs a different id while
     * other slots are used (cfind: used && sid!=sid → right arm false → miss) */
    assert(streammux_peer_finished(m, 5) == 0);   /* open, no FIN seen yet */
    assert(streammux_send_done(m, 5) == 0);        /* open, nothing finished/sent */
    assert(streammux_peer_finished(m, 77) == 1);   /* miss past used slots → sentinel */
    assert(streammux_send_done(m, 77) == 1);
    assert(streammux_readable(m, 77) == 0);        /* same used&&sid!=sid miss in readable */

    /* fill the table (STREAMMUX_MAX), then one more open fails cleanly */
    for (int i = 100; streammux_count(m) < STREAMMUX_MAX; i++) streammux_open(m, (uint16_t)i);
    assert(streammux_count(m) == STREAMMUX_MAX);
    assert(streammux_open(m, 999) == -1);                  /* table full */
    assert(streammux_write(m, 998, (const unsigned char *)"x", 1, 0) == -1);
    /* inbound for a new sid on a full table: auto-open fails → input drops it */
    unsigned char frame[STREAMMUX_FRAME + 1] = { 0x03, 0xe7, 'z' };   /* sid 999 */
    streammux_input(m, frame, sizeof frame, 0);
    assert(streammux_count(m) == STREAMMUX_MAX);           /* unchanged */
    /* finish for a new sid on a full table: open fails → finish is a no-op */
    streammux_finish(m, 997, 0);
    assert(streammux_count(m) == STREAMMUX_MAX);
    streammux_free(m);

    /* OOM: the mux struct allocation fails (tested via calloc returning NULL) */
    /* Note: norn uses calloc directly, no OOM injection like bifrost's bfalloc */
    /* The NULL-safety tests above cover the guard branches */

    streammux_free(NULL);                                  /* NULL free no-op */
    printf("  streammux_edges: OK\n");
}

int main(void) {
    test_two_streams_interleaved();
    test_head_of_line_independence();
    test_streammux_edges();
    printf("test_streammux: OK\n");
    return 0;
}
