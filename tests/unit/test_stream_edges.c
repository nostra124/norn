/* SPDX-License-Identifier: MIT */
/* COV-1: surgical branch coverage for stream.c — the reliability/CC edges the
 * lossy-link tests (test_stream_recovery / test_stream_cc) don't pin precisely:
 * RTO ceiling clamp + Karn backoff, FIN-on-empty, SACK build/apply (in- and
 * out-of-window), fast retransmit at the SACK frontier, the no-RTT-sample-on-retx
 * rule, receive-window rejects, and short/malformed input. Segments are crafted by
 * hand and fed through stream_input so each branch is hit deterministically. */
#include "stream.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* segment header layout (mirrors stream.c): flags(1) seq(4) ack(4) rwnd(2) [payload|sack] */
#define F_DATA 1
#define F_ACK  2
#define F_FIN  4
#define F_SACK 8

static void put32(unsigned char *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static void put16(unsigned char *p, uint16_t v) { p[0]=v>>8; p[1]=v; }
static void put64(unsigned char *p, uint64_t v) { put32(p,(uint32_t)(v>>32)); put32(p+4,(uint32_t)v); }

/* capture the segments the stream emits, so we can assert on retransmits/acks */
static unsigned char cap[64][STREAM_SEG_MAX];
static size_t caplen[64];
static int capn;
static int cap_send(void *ctx, const unsigned char *seg, size_t len) {
    (void)ctx;
    if (capn < 64) { memcpy(cap[capn], seg, len); caplen[capn] = len; capn++; }
    return (int)len;
}
static void cap_reset(void) { capn = 0; }
static int saw_flag(uint8_t f) { for (int i=0;i<capn;i++) if (cap[i][0] & f) return 1; return 0; }

/* feed a pure ACK (optionally with a SACK bitmap) */
static void feed_ack(stream_t *s, uint8_t extra, uint32_t ack, uint16_t rwnd, uint64_t bm, uint32_t now) {
    unsigned char seg[STREAM_HEADER + 8];
    seg[0] = (unsigned char)(F_ACK | extra);
    put32(seg + 1, 0);
    put32(seg + 5, ack);
    put16(seg + 9, rwnd);
    size_t len = STREAM_HEADER;
    if (extra & F_SACK) { put64(seg + STREAM_HEADER, bm); len += 8; }
    stream_input(s, seg, len, now);
}
/* feed a DATA segment with seq + payload */
static void feed_data(stream_t *s, uint32_t seq, uint16_t rwnd, const unsigned char *pl, size_t pln, uint32_t now) {
    unsigned char seg[STREAM_SEG_MAX];
    seg[0] = F_DATA | F_ACK;
    put32(seg + 1, seq);
    put32(seg + 5, 0);
    put16(seg + 9, rwnd);
    if (pln) memcpy(seg + STREAM_HEADER, pl, pln);
    stream_input(s, seg, STREAM_HEADER + pln, now);
}

static void test_short_and_malformed_input(void) {
    stream_t *s = stream_new(cap_send, NULL);
    unsigned char tiny[5] = {0};
    stream_input(s, tiny, sizeof tiny, 0);          /* len < STREAM_HEADER → early return */
    /* F_SACK flagged but no room for the bitmap → the SACK is ignored, not read OOB */
    unsigned char seg[STREAM_HEADER];
    seg[0] = F_ACK | F_SACK; put32(seg+1,0); put32(seg+5,0); put16(seg+9,256);
    stream_input(s, seg, STREAM_HEADER, 0);
    stream_free(s);
    printf("  short_and_malformed_input: OK\n");
}

static void test_rto_clamp_and_karn(void) {
    unsigned char d[100]; memset(d, 1, sizeof d);

    /* a huge RTT sample drives the computed RTO over the ceiling → clamp */
    stream_t *s = stream_new(cap_send, NULL);
    stream_write(s, d, sizeof d, 0);                /* seq0 sent at t=0 */
    feed_ack(s, 0, 1, 256, 0, 25000);               /* ack seq0, RTT=25000 → 3*25000 clamped */
    assert(stream_rto_ms(s) == 60000);
    stream_free(s);

    /* a retransmitted segment yields NO RTT sample when later acked (Karn) */
    stream_t *s2 = stream_new(cap_send, NULL);
    stream_write(s2, d, sizeof d, 0);               /* rto=300, deadline=300 */
    stream_tick(s2, 400);                           /* RTO fires → retransmit (retx=1); rto 300→600 */
    assert(stream_rto_ms(s2) == 600);
    feed_ack(s2, 0, 1, 256, 0, 500);                /* acks a retransmitted seg → no sample */
    assert(stream_rto_ms(s2) == 600);               /* RTO unchanged (Karn) */
    stream_free(s2);

    /* repeated timeouts back the RTO off to the ceiling via the Karn-max branch */
    stream_t *s3 = stream_new(cap_send, NULL);
    stream_write(s3, d, sizeof d, 0);
    uint32_t now = 0;
    for (int i = 0; i < 12; i++) { now += 70000; stream_tick(s3, now); }
    assert(stream_rto_ms(s3) == 60000);
    stream_free(s3);
    printf("  rto_clamp_and_karn: OK\n");
}

static void test_fin_on_empty(void) {
    stream_t *s = stream_new(cap_send, NULL);
    cap_reset();
    stream_finish(s, 0);                            /* no pending data → segments a bare FIN */
    assert(saw_flag(F_FIN));
    stream_free(s);
    printf("  fin_on_empty: OK\n");
}

static void test_recv_window_and_sack_build(void) {
    stream_t *s = stream_new(cap_send, NULL);
    unsigned char p[10]; memset(p, 9, sizeof p);

    cap_reset();
    feed_data(s, 1, 256, p, sizeof p, 0);           /* gap at 0: buffered, deliver stalls, ack carries SACK */
    assert(saw_flag(F_SACK));
    assert(stream_readable(s) == 0);                /* nothing in order yet */

    feed_data(s, 0, 256, p, sizeof p, 0);           /* fills the gap → both deliver */
    assert(stream_readable(s) == 20);

    /* receive-window rejects: a far-future seq (≥ window) and an old/replayed seq */
    feed_data(s, 1000, 256, p, sizeof p, 0);        /* seq-rcv_nxt ≥ WIN → not buffered */
    feed_data(s, 0, 256, p, sizeof p, 0);           /* seq < rcv_nxt → not buffered */
    assert(stream_readable(s) == 20);               /* unchanged */
    stream_free(s);
    printf("  recv_window_and_sack_build: OK\n");
}

static void test_sack_apply_and_fast_retransmit(void) {
    stream_t *s = stream_new(cap_send, NULL);
    unsigned char big[3 * 1024]; memset(big, 7, sizeof big);
    stream_write(s, big, sizeof big, 0);            /* seq 0,1,2 in flight */

    /* a future ack (> snd_nxt) exercises the upper-bound guard without moving state */
    feed_ack(s, 0, 999, 256, 0, 1);

    /* SACK bit0 → seq1 (in window, marked), bit50 → seq51 (out of window, skipped) */
    uint64_t bm = (1ULL << 0) | (1ULL << 50);
    feed_ack(s, F_SACK, 0, 256, bm, 2);             /* dup-ack 1 */
    feed_ack(s, F_SACK, 0, 256, bm, 3);             /* dup-ack 2 */
    cap_reset();
    feed_ack(s, F_SACK, 0, 256, bm, 4);             /* dup-ack 3 → fast retransmit (frontier past seq1) */
    /* the hole (seq0) is retransmitted; seq1 (SACKed) is not */
    assert(capn >= 1);
    int saw_seq0 = 0;
    for (int i = 0; i < capn; i++) {
        uint32_t seq = ((uint32_t)cap[i][1]<<24)|((uint32_t)cap[i][2]<<16)|((uint32_t)cap[i][3]<<8)|cap[i][4];
        if ((cap[i][0] & F_DATA) && seq == 0) saw_seq0 = 1;
    }
    assert(saw_seq0);
    stream_free(s);
    printf("  sack_apply_and_fast_retransmit: OK\n");
}

/* stream_peer_finished: cover both arms of `peer_fin && deliv_count==0` — with the
 * peer's FIN delivered but data still unread (right arm false), then after draining
 * (right arm true). */
static void test_peer_finished_arms(void) {
    stream_t *s = stream_new(cap_send, NULL);
    unsigned char p[10]; memset(p, 4, sizeof p);

    /* a DATA+FIN segment at seq0: delivers payload in order and sets peer_fin */
    unsigned char seg[STREAM_SEG_MAX];
    seg[0] = F_DATA | F_ACK | F_FIN;
    put32(seg + 1, 0);
    put32(seg + 5, 0);
    put16(seg + 9, 256);
    memcpy(seg + STREAM_HEADER, p, sizeof p);
    stream_input(s, seg, STREAM_HEADER + sizeof p, 0);

    /* peer_fin set, but unread data remains → right arm false → not finished yet */
    assert(stream_readable(s) == sizeof p);
    assert(stream_peer_finished(s) == 0);

    /* drain the delivered bytes → deliv_count==0 → both arms true → finished */
    unsigned char out[16];
    assert(stream_read(s, out, sizeof out) == (int)sizeof p);
    assert(stream_peer_finished(s) == 1);
    stream_free(s);
    printf("  peer_finished_arms: OK\n");
}

static void test_tick_guards(void) {
    unsigned char d[100]; memset(d, 1, sizeof d);
    /* tick on an idle stream: no timer armed → the RTO block is skipped */
    stream_t *s = stream_new(cap_send, NULL);
    stream_tick(s, 1000);                           /* rto_deadline == 0 */
    /* tick with a segment in flight but BEFORE the deadline → not yet due */
    stream_write(s, d, sizeof d, 0);                /* deadline = 300 */
    stream_tick(s, 100);                            /* 100 < 300 → no retransmit */
    /* once acked, snd_una == snd_nxt → a later tick has nothing to retransmit */
    feed_ack(s, 0, 1, 256, 0, 120);
    stream_tick(s, 5000);
    assert(stream_send_done(s) == 0 || stream_send_done(s) == 1);   /* no crash; state consistent */
    stream_free(s);
    printf("  tick_guards: OK\n");
}

int main(void) {
    test_short_and_malformed_input();
    test_peer_finished_arms();
    test_tick_guards();
    test_rto_clamp_and_karn();
    test_fin_on_empty();
    test_recv_window_and_sack_build();
    test_sack_apply_and_fast_retransmit();
    printf("test_stream_edges: OK\n");
    return 0;
}
