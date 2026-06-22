/* COV-1: branch coverage for channel.c — the argument guards and handshake-validation
 * failure arms (NULL args, bad magic / type / length / buffer caps, low-order keys,
 * tampered ciphertext) that the happy-path test_channel doesn't reach. */
#include "channel.h"
#include "replaycache.h"
#include "crypto.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define I CHANNEL_INIT_LEN
#define R CHANNEL_RESP_LEN
#define C CHANNEL_CONFIRM_LEN

/* a complete, valid handshake → fixtures (ci/cr established, init/resp/conf bytes) */
static keypair_t ki, kr;
static channel_t ci, cr;
static unsigned char init[I], resp[R], conf[C];
static unsigned char rsi[32], isr[32];

static void handshake(void) {
    assert(channel_hs_build_init(&ci, ki.public_key, init, sizeof init) == I);
    assert(channel_hs_accept(&cr, kr.public_key, kr.secret_key, init, sizeof init, rsi, resp, sizeof resp) == R);
    assert(channel_hs_confirm(&ci, ki.public_key, ki.secret_key, resp, sizeof resp, isr, conf, sizeof conf) == C);
    assert(channel_hs_finish(&cr, rsi, conf, sizeof conf) == 0);
}

static void test_resumption_secret_guards(void) {
    unsigned char out[CHANNEL_RESUMEBYTES];
    channel_t e; memset(&e, 0, sizeof e);
    assert(channel_resumption_secret(NULL, out) == -1);
    assert(channel_resumption_secret(&e, out) == -1);       /* not established */
    assert(channel_resumption_secret(&ci, NULL) == -1);

    /* order-independent combine: exercise BOTH the swap and no-swap orderings */
    channel_t a; memset(&a, 0, sizeof a); a.established = 1;
    memset(a.rx_key, 0xff, 32); memset(a.tx_key, 0x00, 32);   /* rx > tx → swap */
    unsigned char s1[CHANNEL_RESUMEBYTES]; assert(channel_resumption_secret(&a, s1) == 0);
    memset(a.rx_key, 0x00, 32); memset(a.tx_key, 0xff, 32);   /* rx < tx → no swap */
    unsigned char s2[CHANNEL_RESUMEBYTES]; assert(channel_resumption_secret(&a, s2) == 0);
    assert(memcmp(s1, s2, CHANNEL_RESUMEBYTES) == 0);          /* same value either way */
    printf("  resumption_secret_guards: OK\n");
}

static void test_derive_guards(void) {
    channel_t c; assert(channel_gen_ephemeral(&c) == 0);
    unsigned char peer[32]; memset(peer, 7, 32);
    unsigned char psk[CHANNEL_RESUMEBYTES]; memset(psk, 3, sizeof psk);

    assert(channel_gen_ephemeral(NULL) == -1);
    assert(channel_derive(NULL, peer, 1) == -1);
    assert(channel_derive(&c, NULL, 1) == -1);
    /* a low-order (all-zero) peer key makes the X25519 kx reject → derive fails */
    unsigned char zero[32]; memset(zero, 0, 32);
    assert(channel_derive(&c, zero, 1) == -1);
    assert(channel_derive(&c, zero, 0) == -1);

    assert(channel_derive_resumption(NULL, peer, 1, psk) == -1);
    assert(channel_derive_resumption(&c, NULL, 1, psk) == -1);
    assert(channel_derive_resumption(&c, peer, 1, NULL) == -1);
    assert(channel_derive_resumption(&c, zero, 1, psk) == -1);   /* derive-fail path */

    channel_derive_0rtt(NULL, peer, 1, psk);                     /* guards: no crash */
    channel_derive_0rtt(&c, NULL, 1, psk);
    channel_derive_0rtt(&c, peer, 1, NULL);
    printf("  derive_guards: OK\n");
}

static void test_auth_guards(void) {
    unsigned char sig[CHANNEL_SIGBYTES], e1[32], e2[32];
    memset(e1, 1, 32); memset(e2, 2, 32);
    assert(channel_auth_sign(NULL, ki.secret_key, e1, e2) == -1);
    assert(channel_auth_sign(sig, NULL, e1, e2) == -1);
    assert(channel_auth_sign(sig, ki.secret_key, NULL, e2) == -1);
    assert(channel_auth_sign(sig, ki.secret_key, e1, NULL) == -1);
    assert(channel_auth_verify(NULL, ki.public_key, e1, e2) == -1);
    assert(channel_auth_verify(sig, NULL, e1, e2) == -1);
    assert(channel_auth_verify(sig, ki.public_key, NULL, e2) == -1);
    assert(channel_auth_verify(sig, ki.public_key, e1, NULL) == -1);
    printf("  auth_guards: OK\n");
}

static void test_build_init_guards(void) {
    channel_t c; unsigned char out[I];
    assert(channel_hs_build_init(NULL, ki.public_key, out, sizeof out) == -1);
    assert(channel_hs_build_init(&c, NULL, out, sizeof out) == -1);
    assert(channel_hs_build_init(&c, ki.public_key, NULL, sizeof out) == -1);
    assert(channel_hs_build_init(&c, ki.public_key, out, I - 1) == -1);   /* outcap too small */
    printf("  build_init_guards: OK\n");
}

static void test_accept_guards(void) {
    channel_t c; unsigned char out[R], pp[32];
    assert(channel_hs_accept(NULL, kr.public_key, kr.secret_key, init, I, pp, out, sizeof out) == -1);
    assert(channel_hs_accept(&c, NULL, kr.secret_key, init, I, pp, out, sizeof out) == -1);
    assert(channel_hs_accept(&c, kr.public_key, NULL, init, I, pp, out, sizeof out) == -1);
    assert(channel_hs_accept(&c, kr.public_key, kr.secret_key, NULL, I, pp, out, sizeof out) == -1);
    assert(channel_hs_accept(&c, kr.public_key, kr.secret_key, init, I, NULL, out, sizeof out) == -1);
    assert(channel_hs_accept(&c, kr.public_key, kr.secret_key, init, I, pp, NULL, sizeof out) == -1);
    assert(channel_hs_accept(&c, kr.public_key, kr.secret_key, init, I - 1, pp, out, sizeof out) == -1);  /* bad len */
    assert(channel_hs_accept(&c, kr.public_key, kr.secret_key, init, I, pp, out, R - 1) == -1);           /* small outcap */

    unsigned char bad[I]; memcpy(bad, init, I);
    bad[0] ^= 0xff;                                  /* corrupt the magic */
    assert(channel_hs_accept(&c, kr.public_key, kr.secret_key, bad, I, pp, out, sizeof out) == -1);
    memcpy(bad, init, I); bad[4] = 99;               /* unknown message type */
    assert(channel_hs_accept(&c, kr.public_key, kr.secret_key, bad, I, pp, out, sizeof out) == -1);
    memcpy(bad, init, I); memset(bad + 5 + 32, 0, 32);   /* zero ephemeral → derive fails */
    assert(channel_hs_accept(&c, kr.public_key, kr.secret_key, bad, I, pp, out, sizeof out) == -1);
    /* the INIT_VPN type byte is accepted */
    memcpy(bad, init, I); bad[4] = CHANNEL_MSG_INIT_VPN;
    assert(channel_hs_accept(&c, kr.public_key, kr.secret_key, bad, I, pp, out, sizeof out) == R);
    printf("  accept_guards: OK\n");
}

static void test_confirm_finish_guards(void) {
    unsigned char out[C], pp[32];
    /* fresh initiator state for a clean RESP to confirm against */
    channel_t ci2; unsigned char init2[I], resp2[R];
    assert(channel_hs_build_init(&ci2, ki.public_key, init2, sizeof init2) == I);
    channel_t cr2; unsigned char rsi2[32];
    assert(channel_hs_accept(&cr2, kr.public_key, kr.secret_key, init2, I, rsi2, resp2, sizeof resp2) == R);

    assert(channel_hs_confirm(NULL, ki.public_key, ki.secret_key, resp2, R, pp, out, sizeof out) == -1);
    assert(channel_hs_confirm(&ci2, NULL, ki.secret_key, resp2, R, pp, out, sizeof out) == -1);
    assert(channel_hs_confirm(&ci2, ki.public_key, NULL, resp2, R, pp, out, sizeof out) == -1);
    assert(channel_hs_confirm(&ci2, ki.public_key, ki.secret_key, NULL, R, pp, out, sizeof out) == -1);
    assert(channel_hs_confirm(&ci2, ki.public_key, ki.secret_key, resp2, R, NULL, out, sizeof out) == -1);
    assert(channel_hs_confirm(&ci2, ki.public_key, ki.secret_key, resp2, R, pp, NULL, sizeof out) == -1);
    assert(channel_hs_confirm(&ci2, ki.public_key, ki.secret_key, resp2, R - 1, pp, out, sizeof out) == -1);
    assert(channel_hs_confirm(&ci2, ki.public_key, ki.secret_key, resp2, R, pp, out, C - 1) == -1);
    unsigned char bad[R]; memcpy(bad, resp2, R); bad[0] ^= 0xff;          /* magic */
    assert(channel_hs_confirm(&ci2, ki.public_key, ki.secret_key, bad, R, pp, out, sizeof out) == -1);
    memcpy(bad, resp2, R); bad[4] = 99;                                   /* type */
    assert(channel_hs_confirm(&ci2, ki.public_key, ki.secret_key, bad, R, pp, out, sizeof out) == -1);

    /* finish guards */
    assert(channel_hs_finish(NULL, rsi, conf, C) == -1);
    channel_t une; memset(&une, 0, sizeof une);
    assert(channel_hs_finish(&une, rsi, conf, C) == -1);                  /* not established */
    assert(channel_hs_finish(&cr, NULL, conf, C) == -1);
    assert(channel_hs_finish(&cr, rsi, NULL, C) == -1);
    assert(channel_hs_finish(&cr, rsi, conf, C - 1) == -1);               /* bad len */
    unsigned char bc[C]; memcpy(bc, conf, C); bc[0] ^= 0xff;              /* magic */
    assert(channel_hs_finish(&cr, rsi, bc, C) == -1);
    memcpy(bc, conf, C); bc[4] = 99;                                      /* type */
    assert(channel_hs_finish(&cr, rsi, bc, C) == -1);
    memcpy(bc, conf, C); bc[C - 1] ^= 0xff;                               /* tampered sealed sig → open fails */
    assert(channel_hs_finish(&cr, rsi, bc, C) == -1);

    /* transcript's initiator branch: finishing on an initiator-role channel (artificial,
     * verify fails) still exercises the is_initiator arm */
    channel_t cr_as_init = cr; cr_as_init.is_initiator = 1;
    assert(channel_hs_finish(&cr_as_init, rsi, conf, C) != 0);
    printf("  confirm_finish_guards: OK\n");
}

static void test_seal_open_guards(void) {
    unsigned char ct[128], pt[128];
    channel_t une; memset(&une, 0, sizeof une);
    assert(channel_seal(NULL, (const unsigned char *)"x", 1, ct, sizeof ct) == -1);
    assert(channel_seal(&une, (const unsigned char *)"x", 1, ct, sizeof ct) == -1);   /* not established */
    assert(channel_seal(&ci, (const unsigned char *)"x", 1, NULL, sizeof ct) == -1);
    assert(channel_seal(&ci, (const unsigned char *)"x", 1, ct, 4) == -1);            /* outcap too small */
    int n = channel_seal(&ci, (const unsigned char *)"hello", 5, ct, sizeof ct);
    assert(n > 0);

    assert(channel_open(NULL, ct, n, pt, sizeof pt) == -1);
    assert(channel_open(&une, ct, n, pt, sizeof pt) == -1);
    assert(channel_open(&ci, NULL, n, pt, sizeof pt) == -1);
    assert(channel_open(&ci, ct, 4, pt, sizeof pt) == -1);                            /* ctlen too short */
    assert(channel_open(&cr, ct, n, pt, 0) == -1);                                    /* outcap too small */
    ct[30] ^= 0xff;
    assert(channel_open(&cr, ct, n, pt, sizeof pt) == -1);                            /* tampered → auth fail */
    printf("  seal_open_guards: OK\n");
}

static void test_open_any_guards(void) {
    unsigned char ct[128], pt[128];
    int n = channel_seal(&ci, (const unsigned char *)"probe", 5, ct, sizeof ct);   /* ci.tx → cr.rx */
    assert(n > 0);
    assert(channel_open_any(NULL, 1, ct, n, pt, sizeof pt) == -1);
    channel_t *set[] = { NULL, &ci, &cr };          /* NULL slot skipped; ci.rx≠key → no; cr matches */
    assert(channel_open_any(set, 3, ct, n, pt, sizeof pt) == 2);
    channel_t *none[] = { NULL, &ci };              /* no channel opens it */
    assert(channel_open_any(none, 2, ct, n, pt, sizeof pt) == -1);
    printf("  open_any_guards: OK\n");
}

/* the 0-RTT first-flight (build + accept) guard / sub-condition arms */
static void test_0rtt_guards(void) {
    unsigned char psk[CHANNEL_RESUMEBYTES]; memset(psk, 5, sizeof psk);
    const unsigned char early[] = "early";
    unsigned char buf[I + sizeof(early) + CHANNEL_OVERHEAD];
    channel_t c;

    /* build_init_0rtt guards */
    assert(channel_hs_build_init_0rtt(NULL, ki.public_key, psk, early, sizeof early, buf, sizeof buf) == -1);
    assert(channel_hs_build_init_0rtt(&c, NULL, psk, early, sizeof early, buf, sizeof buf) == -1);
    assert(channel_hs_build_init_0rtt(&c, ki.public_key, NULL, early, sizeof early, buf, sizeof buf) == -1);
    assert(channel_hs_build_init_0rtt(&c, ki.public_key, psk, NULL, 4, buf, sizeof buf) == -1);   /* early NULL, len>0 */
    assert(channel_hs_build_init_0rtt(&c, ki.public_key, psk, early, sizeof early, NULL, sizeof buf) == -1);
    assert(channel_hs_build_init_0rtt(&c, ki.public_key, psk, early, sizeof early, buf, 8) == -1);  /* outcap small */

    /* a real 0-RTT INIT to drive the accept arms */
    int il = channel_hs_build_init_0rtt(&c, ki.public_key, psk, early, sizeof early, buf, sizeof buf);
    assert(il > 0);

    channel_t r; unsigned char out[R], pp[32], eout[64]; size_t elen; int status;
    replaycache_t rpc; replaycache_init(&rpc, 60);

    /* guards (101) and early_len==NULL (99) */
    assert(channel_hs_accept_0rtt(NULL, kr.public_key, kr.secret_key, buf, il, psk, &rpc, 1, pp, eout, sizeof eout, &elen, &status, out, sizeof out) == -1);
    assert(channel_hs_accept_0rtt(&r, NULL, kr.secret_key, buf, il, psk, &rpc, 1, pp, eout, sizeof eout, &elen, &status, out, sizeof out) == -1);
    assert(channel_hs_accept_0rtt(&r, kr.public_key, NULL, buf, il, psk, &rpc, 1, pp, eout, sizeof eout, &elen, &status, out, sizeof out) == -1);
    assert(channel_hs_accept_0rtt(&r, kr.public_key, kr.secret_key, NULL, il, psk, &rpc, 1, pp, eout, sizeof eout, &elen, &status, out, sizeof out) == -1);
    assert(channel_hs_accept_0rtt(&r, kr.public_key, kr.secret_key, buf, il, psk, &rpc, 1, NULL, eout, sizeof eout, &elen, &status, out, sizeof out) == -1);
    assert(channel_hs_accept_0rtt(&r, kr.public_key, kr.secret_key, buf, il, psk, &rpc, 1, pp, eout, sizeof eout, &elen, &status, NULL, sizeof out) == -1);
    assert(channel_hs_accept_0rtt(&r, kr.public_key, kr.secret_key, buf, I - 1, psk, &rpc, 1, pp, eout, sizeof eout, &elen, &status, out, sizeof out) == -1);  /* short (102) */
    unsigned char notz[I + 8]; memcpy(notz, buf, I); notz[4] = CHANNEL_MSG_INIT;   /* wrong type (103) */
    assert(channel_hs_accept_0rtt(&r, kr.public_key, kr.secret_key, notz, il, psk, &rpc, 1, pp, eout, sizeof eout, &elen, &status, out, sizeof out) == -1);
    unsigned char badm[sizeof buf]; memcpy(badm, buf, il); badm[0] ^= 0xff;        /* inner accept fails (114) */
    assert(channel_hs_accept_0rtt(&r, kr.public_key, kr.secret_key, badm, il, psk, &rpc, 1, pp, eout, sizeof eout, &elen, &status, out, sizeof out) == -1);

    /* early_out NULL → ternary false (125/126) → BADPSK; status==NULL (137); early_len==NULL (99/134) */
    assert(channel_hs_accept_0rtt(&r, kr.public_key, kr.secret_key, buf, il, psk, &rpc, 1, pp, NULL, 0, NULL, NULL, out, sizeof out) == R);

    /* rpc==NULL → the replay gate is skipped (130) but the flight still opens (OK) */
    int st2 = -1; size_t el2 = 0;
    assert(channel_hs_accept_0rtt(&r, kr.public_key, kr.secret_key, buf, il, psk, NULL, 0, pp, eout, sizeof eout, &el2, &st2, out, sizeof out) == R);
    assert(st2 == CHANNEL_0RTT_OK && el2 == sizeof early);

    /* a no-early-data 0-RTT INIT (early==NULL, len 0) is valid */
    unsigned char nb[I + CHANNEL_OVERHEAD];
    assert(channel_hs_build_init_0rtt(&c, ki.public_key, psk, NULL, 0, nb, sizeof nb) > 0);

    /* early_out set but early_cap==0 → cannot open → BADPSK (early_cap arm) */
    int st3 = -1; size_t el3 = 9;
    assert(channel_hs_accept_0rtt(&r, kr.public_key, kr.secret_key, buf, il, psk, NULL, 0, pp, eout, 0, &el3, &st3, out, sizeof out) == R);
    assert(st3 == CHANNEL_0RTT_BADPSK);

    /* a successful open with early_len==NULL (OK branch, early_len guard) */
    int st4 = -1;
    assert(channel_hs_accept_0rtt(&r, kr.public_key, kr.secret_key, buf, il, psk, NULL, 0, pp, eout, sizeof eout, NULL, &st4, out, sizeof out) == R);
    assert(st4 == CHANNEL_0RTT_OK);
    printf("  0rtt_guards: OK\n");
}

int main(void) {
    assert(crypto_init() == 0);
    assert(crypto_keypair_new(&ki) == 0 && crypto_keypair_new(&kr) == 0);
    handshake();
    test_resumption_secret_guards();
    test_derive_guards();
    test_auth_guards();
    test_build_init_guards();
    test_accept_guards();
    test_confirm_finish_guards();
    test_seal_open_guards();
    test_open_any_guards();
    test_0rtt_guards();
    printf("test_channel_edges: OK\n");
    return 0;
}
