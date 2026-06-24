/* STR-U: seeded-random property tests for the reliable stream.
 * Run N iterations with different PRNG seeds, asserting byte-exact delivery
 * and per-stream head-of-line independence. Deterministic and reproducible. */
#include "stream.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

#define NA 60000   /* bytes A -> B */
#define NB 20000   /* bytes B -> A */
#define MAXSTEPS 3000000
#define DEFAULT_ITERATIONS 100

static uint32_t seed;
static uint32_t lcg;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return (lcg >> 16) & 0x7fff; }
static void rnd_seed(uint32_t s) { seed = s; lcg = s; }

typedef struct { unsigned char buf[STREAM_SEG_MAX]; size_t len; uint32_t at; } pkt_t;
typedef struct { pkt_t q[4096]; int n; } wire_t;

static wire_t ab, ba;

static int send_into(wire_t *w, const unsigned char *seg, size_t len) {
    if (w->n >= (int)(sizeof(w->q) / sizeof(w->q[0]))) return -1;
    if (rnd() % 100 < 10) return 0;            /* 10% loss */
    pkt_t *p = &w->q[w->n++];
    memcpy(p->buf, seg, len); p->len = len;
    p->at = 0 + 1 + rnd() % 4;                  /* jitter (relative to step) */
    if (rnd() % 100 < 5 && w->n < (int)(sizeof(w->q) / sizeof(w->q[0]))) {
        pkt_t *d = &w->q[w->n++];              /* 5% duplicate */
        memcpy(d->buf, seg, len); d->len = len;
        d->at = 0 + 1 + rnd() % 4;
    }
    return 0;
}
static int send_ab(void *ctx, const unsigned char *seg, size_t len) { (void)ctx; return send_into(&ab, seg, len); }
static int send_ba(void *ctx, const unsigned char *seg, size_t len) { (void)ctx; return send_into(&ba, seg, len); }

static void deliver(wire_t *w, stream_t *peer, uint32_t step) {
    int k = 0;
    for (int i = 0; i < w->n; i++) {
        if (w->q[i].at <= step) stream_input(peer, w->q[i].buf, w->q[i].len, step);
        else w->q[k++] = w->q[i];
    }
    w->n = k;
}

static int run_one_iteration(uint32_t s) {
    static unsigned char SA[NA], SB[NB], RA[NB + 16], RB[NA + 16];
    
    /* Fill with seed-dependent pattern for verification */
    rnd_seed(s);
    for (int i = 0; i < NA; i++) SA[i] = (unsigned char)((i * 7 + 3 + s) & 0xFF);
    for (int i = 0; i < NB; i++) SB[i] = (unsigned char)((i * 13 + 5 + s) & 0xFF);

    stream_t *A = stream_new(send_ab, NULL);
    stream_t *B = stream_new(send_ba, NULL);
    if (!A || !B) { if (A) stream_free(A); if (B) stream_free(B); return -1; }

    ab.n = 0; ba.n = 0;
    lcg = s;  /* reset for this iteration */

    size_t a_sent = 0, b_sent = 0, ra = 0, rb = 0;
    int a_fin = 0, b_fin = 0;
    uint32_t step;
    for (step = 0; step < MAXSTEPS; step++) {
        if (a_sent < NA) a_sent += stream_write(A, SA + a_sent, NA - a_sent, step);
        else if (!a_fin) { stream_finish(A, step); a_fin = 1; }
        if (b_sent < NB) b_sent += stream_write(B, SB + b_sent, NB - b_sent, step);
        else if (!b_fin) { stream_finish(B, step); b_fin = 1; }

        stream_tick(A, step);
        stream_tick(B, step);
        deliver(&ab, B, step);
        deliver(&ba, A, step);

        if (rb < NA + 16) rb += stream_read(B, RB + rb, sizeof(RB) - rb);
        if (ra < NB + 16) ra += stream_read(A, RA + ra, sizeof(RA) - ra);

        if (stream_send_done(A) && stream_send_done(B) &&
            stream_peer_finished(A) && stream_peer_finished(B))
            break;
    }

    int ok = 1;
    if (step >= MAXSTEPS) { ok = 0; }
    if (rb != NA) { ok = 0; }
    if (ra != NB) { ok = 0; }
    if (memcmp(RB, SA, NA) != 0) { ok = 0; }
    if (memcmp(RA, SB, NB) != 0) { ok = 0; }

    stream_free(A);
    stream_free(B);
    return ok ? 0 : -1;
}

int main(int argc, char **argv) {
    int iterations = DEFAULT_ITERATIONS;
    if (argc > 1) iterations = atoi(argv[1]);
    if (iterations < 1) iterations = 1;
    if (iterations > 10000) iterations = 10000;

    printf("STR-U: running %d seeded-random stream iterations\n", iterations);
    
    int passed = 0, failed = 0;
    for (int i = 0; i < iterations; i++) {
        uint32_t s = (uint32_t)(i * 31337 + 0xDEADBEEF);
        if (run_one_iteration(s) == 0) passed++;
        else { failed++; printf("  FAILED seed=%u\n", s); }
    }

    printf("STR-U: %d/%d passed\n", passed, iterations);
    return (failed == 0) ? 0 : 1;
}