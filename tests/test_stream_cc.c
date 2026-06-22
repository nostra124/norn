/* FEAT-073 (1/n) regression: AIMD congestion control + dynamic window. The send
 * window is no longer a static 64 segments — a congestion window (cwnd) grows in
 * slow start, switches to additive increase, and multiplicatively decreases on
 * loss. In-flight is bounded by cwnd. Deterministic harness (no RNG): the test
 * controls the clock and chooses per-packet delivery, observing via the public
 * API + stream_cwnd()/stream_inflight() accessors. */
#include "stream.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

typedef struct { unsigned char buf[STREAM_SEG_MAX]; size_t len; } cap_t;
typedef struct { cap_t q[65536]; int n; } capq;
static capq QA, QB;
static uint32_t CLK;

static void reset(void) { QA.n = QB.n = 0; CLK = 0; }
static int sendA(void *c, const unsigned char *s, size_t l) {
    (void)c; if (QA.n < (int)(sizeof QA.q / sizeof QA.q[0])) { memcpy(QA.q[QA.n].buf, s, l); QA.q[QA.n].len = l; QA.n++; } return 0;
}
static int sendB(void *c, const unsigned char *s, size_t l) {
    (void)c; if (QB.n < (int)(sizeof QB.q / sizeof QB.q[0])) { memcpy(QB.q[QB.n].buf, s, l); QB.q[QB.n].len = l; QB.n++; } return 0;
}
static void deliver(capq *q, stream_t *peer) {
    for (int i = 0; i < q->n; i++) stream_input(peer, q->q[i].buf, q->q[i].len, CLK);
    q->n = 0;
}
static void drain(stream_t *s) { unsigned char tmp[65536]; while (stream_read(s, tmp, sizeof tmp) > 0) {} }

static unsigned char DATA[64 * 1024];

/* cwnd grows past the old static 64-segment cap on a clean (lossless) high-RTT
 * link, and in-flight never exceeds cwnd. */
static void test_slow_start_and_dynamic_window(void) {
    reset();
    for (size_t i = 0; i < sizeof DATA; i++) DATA[i] = (unsigned char)(i * 7 + 1);
    stream_t *A = stream_new(sendA, NULL), *B = stream_new(sendB, NULL);
    assert(A && B);
    const uint32_t RTT = 20;
    uint32_t cwnd0 = stream_cwnd(A);
    assert(cwnd0 >= 1 && cwnd0 <= 64);            /* a modest initial window */
    int grew_past_64 = 0;
    for (int r = 0; r < 200; r++) {
        stream_write(A, DATA, sizeof DATA, CLK);  /* keep the pipe full */
        stream_tick(A, CLK);
        assert(stream_inflight(A) <= stream_cwnd(A));   /* never overshoot cwnd */
        CLK += RTT;
        deliver(&QA, B);                          /* B acks (no loss) */
        deliver(&QB, A);                          /* A advances + grows cwnd */
        drain(B);
        if (stream_cwnd(A) > 64) { grew_past_64 = 1; break; }
    }
    assert(grew_past_64);                         /* the static 64-seg cap is lifted */
    stream_free(A); stream_free(B);
    printf("  slow_start_and_dynamic_window: OK\n");
}

/* a loss multiplicatively decreases cwnd (AIMD), it doesn't reset to the floor
 * silently or keep growing. */
static void test_loss_shrinks_cwnd(void) {
    reset();
    for (size_t i = 0; i < sizeof DATA; i++) DATA[i] = (unsigned char)(i * 11 + 3);
    stream_t *A = stream_new(sendA, NULL), *B = stream_new(sendB, NULL);
    assert(A && B);
    const uint32_t RTT = 20;
    for (int r = 0; r < 40 && stream_cwnd(A) < 32; r++) {   /* grow cwnd first */
        stream_write(A, DATA, sizeof DATA, CLK);
        stream_tick(A, CLK);
        CLK += RTT;
        deliver(&QA, B); deliver(&QB, A); drain(B);
    }
    uint32_t cwnd_before = stream_cwnd(A);
    assert(cwnd_before >= 4);

    /* a window of data is sent and entirely lost; the RTO collapses cwnd */
    stream_write(A, DATA, sizeof DATA, CLK);
    stream_tick(A, CLK);
    QA.n = 0;                                     /* drop everything in flight */
    CLK += stream_rto_ms(A) + 5;
    stream_tick(A, CLK);                          /* RTO fires */
    uint32_t cwnd_after = stream_cwnd(A);
    assert(cwnd_after < cwnd_before);             /* multiplicative decrease */
    stream_free(A); stream_free(B);
    printf("  loss_shrinks_cwnd: OK (%u -> %u)\n", cwnd_before, cwnd_after);
}

/* FEAT-073 (2/n): a stalled receiver application (not draining) shrinks its
 * advertised window, throttling the sender below cwnd — flow control, not
 * congestion (cwnd stays high; no loss). */
static void test_flow_control(void) {
    reset();
    for (size_t i = 0; i < sizeof DATA; i++) DATA[i] = (unsigned char)(i * 5 + 9);
    stream_t *A = stream_new(sendA, NULL), *B = stream_new(sendB, NULL);
    assert(A && B);
    const uint32_t RTT = 20;
    /* phase 1: grow cwnd large while B drains normally */
    for (int r = 0; r < 80 && stream_cwnd(A) < 64; r++) {
        stream_write(A, DATA, sizeof DATA, CLK);
        stream_tick(A, CLK);
        CLK += RTT;
        deliver(&QA, B); deliver(&QB, A); drain(B);
    }
    uint32_t big_cwnd = stream_cwnd(A);
    assert(big_cwnd >= 64);
    /* phase 2: B's app STOPS draining — its receive buffer fills, rwnd → 0 */
    for (int r = 0; r < 40; r++) {
        stream_write(A, DATA, sizeof DATA, CLK);
        stream_tick(A, CLK);
        CLK += RTT;
        deliver(&QA, B); deliver(&QB, A);          /* no drain(B) */
    }
    assert(stream_cwnd(A) >= 64);                   /* flow control, NOT congestion */
    assert(stream_inflight(A) < big_cwnd);         /* throttled below cwnd by rwnd */
    assert(stream_inflight(A) <= 4);               /* ~zero window: receiver buffer full */
    /* phase 3: B drains → the persist probe elicits a reopened window → sender resumes */
    drain(B);
    CLK += stream_rto_ms(A) + 1;                   /* let the persist probe fire now */
    for (int r = 0; r < 6; r++) {
        stream_tick(A, CLK);                       /* resend probe / send once window reopens */
        deliver(&QA, B); deliver(&QB, A); drain(B);
        stream_write(A, DATA, sizeof DATA, CLK);
        CLK += RTT;
    }
    assert(stream_inflight(A) > 4);                /* unstuck after the window reopened */
    stream_free(A); stream_free(B);
    printf("  flow_control: OK (cwnd=%u held; in-flight throttled to ~rwnd)\n", big_cwnd);
}

int main(void) {
    test_slow_start_and_dynamic_window();
    test_loss_shrinks_cwnd();
    test_flow_control();
    printf("test_stream_cc: OK\n");
    return 0;
}
