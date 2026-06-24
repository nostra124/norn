/* FEAT-072 regression: stream loss recovery — adaptive RTO (RTT-estimated, Karn),
 * selective retransmit (SACK), and fast retransmit (3 dup-ACKs before the RTO).
 *
 * Deterministic harness (no RNG): the test fully controls the clock and chooses,
 * per packet, whether to deliver or drop it — so it can assert *which* segments
 * are retransmitted and *when*, through the public API + the stream_rto_ms()
 * accessor only. The end-to-end fuzz (drop/reorder/dup) lives in test_stream.c
 * and stays the convergence regression. */
#include "stream.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#define F_DATA 1
#define F_ACK  2
#define F_FIN  4
#define F_SACK 8

static uint32_t G32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

typedef struct { unsigned char buf[STREAM_SEG_MAX]; size_t len; } cap_t;
typedef struct { cap_t q[16384]; int n; } capq;

static capq QA, QB;             /* segments emitted by A / by B */
static unsigned cnt[4096];      /* how many times A emitted a DATA/FIN seg, by seq */
static uint32_t CLK;

static void reset(void) { QA.n = QB.n = 0; memset(cnt, 0, sizeof cnt); CLK = 0; }

static int sendA(void *c, const unsigned char *s, size_t l) {
    (void)c;
    if (QA.n < (int)(sizeof QA.q / sizeof QA.q[0])) {
        memcpy(QA.q[QA.n].buf, s, l); QA.q[QA.n].len = l; QA.n++;
    }
    if ((s[0] & (F_DATA | F_FIN)) && l >= 5) { uint32_t seq = G32(s + 1); if (seq < 4096) cnt[seq]++; }
    return 0;
}
static int sendB(void *c, const unsigned char *s, size_t l) {
    (void)c;
    if (QB.n < (int)(sizeof QB.q / sizeof QB.q[0])) {
        memcpy(QB.q[QB.n].buf, s, l); QB.q[QB.n].len = l; QB.n++;
    }
    return 0;
}

/* feed every queued packet to peer at CLK, then empty the queue */
static void deliver(capq *q, stream_t *peer) {
    for (int i = 0; i < q->n; i++) stream_input(peer, q->q[i].buf, q->q[i].len, CLK);
    q->n = 0;
}
/* deliver all but the DATA segment carrying seq==drop (simulate that one lost) */
static void deliver_except(capq *q, stream_t *peer, uint32_t drop) {
    for (int i = 0; i < q->n; i++) {
        const unsigned char *b = q->q[i].buf;
        if ((b[0] & F_DATA) && G32(b + 1) == drop) continue;
        stream_input(peer, b, q->q[i].len, CLK);
    }
    q->n = 0;
}
static void drain(stream_t *s) { unsigned char tmp[8192]; while (stream_read(s, tmp, sizeof tmp) > 0) {} }

static unsigned char DATA[16 * 1024];

/* (a) adaptive RTO converges toward the measured RTT, and a perfectly-acked flow
 *     never retransmits a segment (no spurious retransmit). */
static void test_adaptive_rto_no_spurious(void) {
    reset();
    for (size_t i = 0; i < sizeof DATA; i++) DATA[i] = (unsigned char)(i * 7 + 1);
    stream_t *A = stream_new(sendA, NULL), *B = stream_new(sendB, NULL);
    assert(A && B);
    const uint32_t RTT = 40;
    for (int r = 0; r < 40; r++) {
        stream_write(A, DATA, 1500, CLK);   /* ~1-2 segs/round */
        stream_tick(A, CLK);
        CLK += RTT;                          /* one RTT passes in flight */
        deliver(&QA, B);                     /* B receives data, emits acks */
        deliver(&QB, A);                     /* A receives acks: RTT sample == RTT */
        drain(B);
    }
    uint32_t rto = stream_rto_ms(A);
    assert(rto < 300);                       /* adapted down from the 300ms default */
    assert(rto >= RTT);                      /* but still sane (>= measured RTT) */
    for (int i = 0; i < 4096; i++) assert(cnt[i] <= 1);   /* nothing sent twice */
    stream_free(A); stream_free(B);
    printf("  adaptive_rto_no_spurious: OK (rto=%ums)\n", rto);
}

/* (b)+(c) one segment of a 10-seg burst is lost: the SACK'd dup-ACKs trigger a
 *     fast retransmit of ONLY the hole, before any RTO (clock never advances). */
static void test_selective_fast_retransmit(void) {
    reset();
    for (size_t i = 0; i < sizeof DATA; i++) DATA[i] = (unsigned char)(i * 11 + 3);
    stream_t *A = stream_new(sendA, NULL), *B = stream_new(sendB, NULL);
    assert(A && B);
    const uint32_t K = 3;

    stream_write(A, DATA, 10 * 1024, CLK);   /* 10 segments, seq 0..9 */
    for (int i = 0; i < 10; i++) assert(cnt[i] == 1);

    deliver_except(&QA, B, K);               /* B gets 0..2 in order, 4..9 out of order */
    deliver(&QB, A);                         /* 6 dup-ACKs (cum=3, SACK 4..9) reach A */

    assert(cnt[K] == 2);                     /* hole retransmitted exactly once more */
    for (int i = 0; i < 10; i++)
        if ((uint32_t)i != K) assert(cnt[i] == 1);   /* SACK'd / acked segs NOT resent */

    deliver(&QA, B); deliver(&QB, A); drain(B);       /* hole arrives, stream drains */
    stream_finish(A, CLK);
    for (int g = 0; g < 50 && !stream_send_done(A); g++) {
        stream_tick(A, CLK); deliver(&QA, B); deliver(&QB, A); drain(B); CLK += 10;
    }
    assert(stream_send_done(A));
    stream_free(A); stream_free(B);
    printf("  selective_fast_retransmit: OK\n");
}

/* Karn: an RTT sample from a *retransmitted* (ambiguous) segment must be ignored,
 *     so a fast ack-after-retransmit can't poison the RTO downward. */
static void test_karn_no_poison(void) {
    reset();
    for (size_t i = 0; i < sizeof DATA; i++) DATA[i] = (unsigned char)(i * 5 + 9);
    stream_t *A = stream_new(sendA, NULL), *B = stream_new(sendB, NULL);
    assert(A && B);
    const uint32_t RTT = 100;
    for (int r = 0; r < 12; r++) {           /* establish SRTT ~ 100 */
        stream_write(A, DATA, 1024, CLK);
        stream_tick(A, CLK);
        CLK += RTT;
        deliver(&QA, B); deliver(&QB, A); drain(B);
    }
    uint32_t rto_before = stream_rto_ms(A);
    assert(rto_before < 300 && rto_before >= 50);

    stream_write(A, DATA, 1024, CLK);        /* one more segment ... */
    QA.n = 0;                                /* ... and it is LOST (dropped) */
    CLK += rto_before + 5;
    stream_tick(A, CLK);                     /* RTO fires → retransmit (Karn backoff) */
    assert(stream_rto_ms(A) >= rto_before);  /* backed off, not collapsed */

    CLK += 5;                                /* ambiguous ack ~5ms after retransmit */
    deliver(&QA, B); deliver(&QB, A);
    assert(stream_rto_ms(A) >= rto_before);  /* NOT poisoned down to ~5ms (Karn held) */
    stream_free(A); stream_free(B);
    printf("  karn_no_poison: OK\n");
}

int main(void) {
    test_adaptive_rto_no_spurious();
    test_selective_fast_retransmit();
    test_karn_no_poison();
    printf("test_stream_recovery: OK\n");
    return 0;
}
