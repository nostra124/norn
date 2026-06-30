/* SPDX-License-Identifier: MIT */
/* STR-U: seeded-random property tests for the streammux (logical stream multiplexer).
 * Run N iterations with different PRNG seeds, asserting byte-exact delivery and
 * head-of-line independence across logical streams. Deterministic and reproducible. */
#include "streammux.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

#define N1 40000
#define N2 25000
#define MAXSTEPS 4000000
#define DEFAULT_ITERATIONS 100

static uint32_t seed;
static uint32_t lcg;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return (lcg >> 16) & 0x7fff; }
static void rnd_seed(uint32_t s) { seed = s; lcg = s; }

typedef struct { unsigned char buf[STREAMMUX_FRAME + STREAM_SEG_MAX]; size_t len; uint32_t at; } pkt_t;
typedef struct { pkt_t q[8192]; int n; } wire_t;
static wire_t ab, ba;

__attribute__((unused))
static uint16_t frame_sid(const unsigned char *b) { return (uint16_t)((b[0] << 8) | b[1]); }

static int send_into(wire_t *w, const unsigned char *seg, size_t len) {
    if (w->n >= (int)(sizeof w->q / sizeof w->q[0])) return -1;
    if (rnd() % 100 < 10) return 0;                            /* 10% loss */
    pkt_t *p = &w->q[w->n++];
    memcpy(p->buf, seg, len); p->len = len; p->at = 0 + 1 + rnd() % 4;   /* jitter */
    if (rnd() % 100 < 5 && w->n < (int)(sizeof w->q / sizeof w->q[0])) {
        pkt_t *d = &w->q[w->n++]; memcpy(d->buf, seg, len); d->len = len; d->at = 0 + 1 + rnd() % 4;
    }
    return 0;
}
static int sendA(void *c, const unsigned char *s, size_t l) { (void)c; return send_into(&ab, s, l); }
static int sendB(void *c, const unsigned char *s, size_t l) { (void)c; return send_into(&ba, s, l); }
static void deliver(wire_t *w, streammux_t *peer, uint32_t step) {
    int k = 0;
    for (int i = 0; i < w->n; i++) {
        if (w->q[i].at <= step) streammux_input(peer, w->q[i].buf, w->q[i].len, step);
        else w->q[k++] = w->q[i];
    }
    w->n = k;
}

static unsigned char S1[N1], S2[N2], R1[N1 + 16], R2[N2 + 16];

static int run_two_streams_interleaved(uint32_t s) {
    ab.n = ba.n = 0;
    rnd_seed(s);
    for (int i = 0; i < N1; i++) S1[i] = (unsigned char)((i * 7 + 1 + s) & 0xFF);
    for (int i = 0; i < N2; i++) S2[i] = (unsigned char)((i * 13 + 5 + s) & 0xFF);

    streammux_t *A = streammux_new(sendA, NULL), *B = streammux_new(sendB, NULL);
    if (!A || !B) { if (A) streammux_free(A); if (B) streammux_free(B); return -1; }
    if (streammux_open(A, 1) != 0 || streammux_open(A, 2) != 0) { streammux_free(A); streammux_free(B); return -1; }

    size_t w1 = 0, w2 = 0, r1 = 0, r2 = 0;
    uint32_t step;
    for (step = 0; step < MAXSTEPS; step++) {
        if (w1 < N1) w1 += (size_t)streammux_write(A, 1, S1 + w1, N1 - w1, step);
        if (w2 < N2) w2 += (size_t)streammux_write(A, 2, S2 + w2, N2 - w2, step);
        streammux_tick(A, step); streammux_tick(B, step);
        deliver(&ab, B, step); deliver(&ba, A, step);
        if (r1 < N1 + 16) r1 += (size_t)streammux_read(B, 1, R1 + r1, sizeof(R1) - r1);
        if (r2 < N2 + 16) r2 += (size_t)streammux_read(B, 2, R2 + r2, sizeof(R2) - r2);
        if (r1 == N1 && r2 == N2) break;
    }

    int ok = 1;
    if (step >= MAXSTEPS) ok = 0;
    if (r1 != N1) ok = 0;
    if (r2 != N2) ok = 0;
    if (memcmp(R1, S1, N1) != 0) ok = 0;
    if (memcmp(R2, S2, N2) != 0) ok = 0;

    streammux_free(A); streammux_free(B);
    return ok ? 0 : -1;
}

static int run_hol_independence(uint32_t s) {
    ab.n = ba.n = 0;
    rnd_seed(s);
    for (int i = 0; i < N2; i++) S2[i] = (unsigned char)((i * 3 + 7 + s) & 0xFF);

    streammux_t *A = streammux_new(sendA, NULL), *B = streammux_new(sendB, NULL);
    if (!A || !B) { if (A) streammux_free(A); if (B) streammux_free(B); return -1; }
    if (streammux_open(A, 1) != 0 || streammux_open(A, 2) != 0) { streammux_free(A); streammux_free(B); return -1; }

    /* Block stream 1 by never opening it for reading on B */
    size_t w2 = 0, r2 = 0;
    unsigned char blob[2000];
    for (size_t i = 0; i < sizeof blob; i++) blob[i] = (unsigned char)((i + s) & 0xFF);

    uint32_t step;
    for (step = 0; step < 2000000; step++) {
        streammux_write(A, 1, blob, sizeof blob, step);  /* stream 1: never read */
        if (w2 < N2) w2 += (size_t)streammux_write(A, 2, S2 + w2, N2 - w2, step);
        streammux_tick(A, step); streammux_tick(B, step);
        deliver(&ab, B, step); deliver(&ba, A, step);
        if (r2 < N2) r2 += (size_t)streammux_read(B, 2, R2 + r2, sizeof(R2) - r2);
        if (r2 == N2) break;
    }

    int ok = 1;
    if (r2 != N2) ok = 0;
    if (memcmp(R2, S2, N2) != 0) ok = 0;

    streammux_free(A); streammux_free(B);
    return ok ? 0 : -1;
}

int main(int argc, char **argv) {
    int iterations = DEFAULT_ITERATIONS;
    if (argc > 1) iterations = atoi(argv[1]);
    if (iterations < 1) iterations = 1;
    if (iterations > 10000) iterations = 10000;

    printf("STR-U streammux: running %d seeded-random iterations\n", iterations);
    
    int passed = 0, failed = 0;
    for (int i = 0; i < iterations; i++) {
        uint32_t s = (uint32_t)(i * 31337 + 0xCAFEBABE);
        if (run_two_streams_interleaved(s) == 0) passed++;
        else { failed++; printf("  FAILED two_streams seed=%u\n", s); }
    }
    
    for (int i = 0; i < iterations; i++) {
        uint32_t s = (uint32_t)(i * 31337 + 0xDEADC0DE);
        if (run_hol_independence(s) == 0) passed++;
        else { failed++; printf("  FAILED hol_independence seed=%u\n", s); }
    }

    printf("STR-U streammux: %d/%d passed\n", passed, iterations * 2);
    return (failed == 0) ? 0 : 1;
}