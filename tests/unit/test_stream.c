/* SPDX-License-Identifier: MIT */
/* Unit test for the reliable stream (FEAT-003 slice 3a). Two streams exchange
 * data across a simulated link that drops, reorders and duplicates datagrams;
 * the bytes must arrive complete and in order on both ends. */
#include "stream.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#define NA 60000   /* bytes A -> B */
#define NB 20000   /* bytes B -> A */
#define MAXSTEPS 3000000

static uint32_t CLK;
static uint32_t lcg = 0x12345678;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return (lcg >> 16) & 0x7fff; }

typedef struct { unsigned char buf[STREAM_SEG_MAX]; size_t len; uint32_t at; } pkt_t;
typedef struct { pkt_t q[4096]; int n; } wire_t;

static wire_t ab, ba;  /* A->B and B->A */

static int send_into(wire_t *w, const unsigned char *seg, size_t len) {
    if (w->n >= (int)(sizeof(w->q) / sizeof(w->q[0]))) return -1;
    if (rnd() % 100 < 10) return 0;            /* 10% loss (pretend it sent) */
    pkt_t *p = &w->q[w->n++];
    memcpy(p->buf, seg, len); p->len = len;
    p->at = CLK + 1 + rnd() % 4;               /* jitter → reordering */
    if (rnd() % 100 < 5 && w->n < (int)(sizeof(w->q) / sizeof(w->q[0]))) {
        pkt_t *d = &w->q[w->n++];              /* 5% duplicate */
        memcpy(d->buf, seg, len); d->len = len;
        d->at = CLK + 1 + rnd() % 4;
    }
    return 0;
}
static int send_ab(void *ctx, const unsigned char *seg, size_t len) { (void)ctx; return send_into(&ab, seg, len); }
static int send_ba(void *ctx, const unsigned char *seg, size_t len) { (void)ctx; return send_into(&ba, seg, len); }

static void deliver(wire_t *w, stream_t *peer) {
    int k = 0;
    for (int i = 0; i < w->n; i++) {
        if (w->q[i].at <= CLK) stream_input(peer, w->q[i].buf, w->q[i].len, CLK);
        else w->q[k++] = w->q[i];
    }
    w->n = k;
}

int main(void) {
    static unsigned char SA[NA], SB[NB], RA[NB + 16], RB[NA + 16];
    for (int i = 0; i < NA; i++) SA[i] = (unsigned char)(i * 7 + 3);
    for (int i = 0; i < NB; i++) SB[i] = (unsigned char)(i * 13 + 5);

    stream_t *A = stream_new(send_ab, NULL);
    stream_t *B = stream_new(send_ba, NULL);
    assert(A && B);

    size_t a_sent = 0, b_sent = 0, ra = 0, rb = 0;
    int a_fin = 0, b_fin = 0;
    long step;
    for (step = 0; step < MAXSTEPS; step++) {
        if (a_sent < NA) a_sent += stream_write(A, SA + a_sent, NA - a_sent, CLK);
        else if (!a_fin) { stream_finish(A, CLK); a_fin = 1; }
        if (b_sent < NB) b_sent += stream_write(B, SB + b_sent, NB - b_sent, CLK);
        else if (!b_fin) { stream_finish(B, CLK); b_fin = 1; }

        stream_tick(A, CLK);
        stream_tick(B, CLK);
        deliver(&ab, B);
        deliver(&ba, A);

        if (rb < NA + 16) rb += stream_read(B, RB + rb, sizeof(RB) - rb);   /* A's data at B */
        if (ra < NB + 16) ra += stream_read(A, RA + ra, sizeof(RA) - ra);   /* B's data at A */

        if (stream_send_done(A) && stream_send_done(B) &&
            stream_peer_finished(A) && stream_peer_finished(B))
            break;
        CLK++;
    }

    assert(step < MAXSTEPS);                 /* converged */
    assert(rb == NA && memcmp(RB, SA, NA) == 0);   /* A->B intact and ordered */
    assert(ra == NB && memcmp(RA, SB, NB) == 0);   /* B->A intact and ordered */

    stream_free(A);
    stream_free(B);
    printf("test_stream: OK (%ld steps)\n", step);
    return 0;
}
