/* Unit test for the secure-channel session crypto (FEAT-002 slice 1). */
#include "channel.h"
#include "crypto.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* An external signer (ssh-agent stand-in) that signs with a raw key held in ud. */
static int ext_signer(void *ud, unsigned char sig[CHANNEL_SIGBYTES],
                      const unsigned char *msg, size_t msglen) {
    return bf_sign(sig, msg, msglen, (const unsigned char *)ud);
}
/* A signer that always fails — models an unreachable agent. */
static int failing_signer(void *ud, unsigned char sig[CHANNEL_SIGBYTES],
                          const unsigned char *msg, size_t msglen) {
    (void)ud;
    (void)sig;
    (void)msg;
    (void)msglen;
    return -1;
}

int main(void) {
    assert(crypto_init() == 0);

    channel_t a, b;
    assert(channel_gen_ephemeral(&a) == 0);
    assert(channel_gen_ephemeral(&b) == 0);
    /* connect side = initiator, accept side = not */
    assert(channel_derive(&a, b.eph_pub, 1) == 0);
    assert(channel_derive(&b, a.eph_pub, 0) == 0);

    /* session keys cross between the two ends */
    assert(memcmp(a.tx_key, b.rx_key, 32) == 0);
    assert(memcmp(a.rx_key, b.tx_key, 32) == 0);

    unsigned char sealed[512], opened[512];

    /* A -> B round-trip */
    const char *m1 = "hello over the encrypted channel";
    int sl = channel_seal(&a, (const unsigned char *)m1, strlen(m1), sealed, sizeof(sealed));
    assert(sl == (int)(strlen(m1) + CHANNEL_OVERHEAD));
    int ol = channel_open(&b, sealed, sl, opened, sizeof(opened));
    assert(ol == (int)strlen(m1) && memcmp(opened, m1, ol) == 0);

    /* B -> A round-trip */
    const char *m2 = "reply from the other side";
    int sl2 = channel_seal(&b, (const unsigned char *)m2, strlen(m2), sealed, sizeof(sealed));
    int ol2 = channel_open(&a, sealed, sl2, opened, sizeof(opened));
    assert(ol2 == (int)strlen(m2) && memcmp(opened, m2, ol2) == 0);

    /* tampering is detected */
    sl = channel_seal(&a, (const unsigned char *)m1, strlen(m1), sealed, sizeof(sealed));
    sealed[24] ^= 0x80;  /* flip a byte in the MAC */
    assert(channel_open(&b, sealed, sl, opened, sizeof(opened)) == -1);

    /* tiny output buffer rejected */
    assert(channel_seal(&a, (const unsigned char *)m1, strlen(m1), sealed, 4) == -1);

    /* handshake auth: sign the ephemeral transcript with an ed25519 identity */
    keypair_t ka, kb;
    assert(crypto_keypair_new(&ka) == 0);
    assert(crypto_keypair_new(&kb) == 0);
    unsigned char sig[CHANNEL_SIGBYTES];
    assert(channel_auth_sign(sig, ka.secret_key, a.eph_pub, b.eph_pub) == 0);
    assert(channel_auth_verify(sig, ka.public_key, a.eph_pub, b.eph_pub) == 0);   /* valid */
    assert(channel_auth_verify(sig, kb.public_key, a.eph_pub, b.eph_pub) != 0);   /* wrong identity */
    assert(channel_auth_verify(sig, ka.public_key, b.eph_pub, b.eph_pub) != 0);   /* tampered transcript */

    /* full 3-message handshake: initiator (ka) <-> responder (kb) */
    keypair_t ki, kr;
    assert(crypto_keypair_new(&ki) == 0);
    assert(crypto_keypair_new(&kr) == 0);
    channel_t ci, cr;
    unsigned char init[CHANNEL_INIT_LEN], resp[CHANNEL_RESP_LEN], conf[CHANNEL_CONFIRM_LEN];
    unsigned char r_sees_i[32], i_sees_r[32];

    assert(channel_hs_build_init(&ci, ki.public_key, init, sizeof(init)) == CHANNEL_INIT_LEN);
    assert(channel_hs_accept(&cr, kr.public_key, kr.secret_key, init, sizeof(init),
                             r_sees_i, resp, sizeof(resp)) == CHANNEL_RESP_LEN);
    assert(channel_hs_confirm(&ci, ki.public_key, ki.secret_key, resp, sizeof(resp),
                              i_sees_r, conf, sizeof(conf)) == CHANNEL_CONFIRM_LEN);
    assert(channel_hs_finish(&cr, r_sees_i, conf, sizeof(conf)) == 0);

    /* each end learned the other's true identity, session keys cross */
    assert(memcmp(r_sees_i, ki.public_key, 32) == 0);
    assert(memcmp(i_sees_r, kr.public_key, 32) == 0);
    assert(memcmp(ci.tx_key, cr.rx_key, 32) == 0 && memcmp(ci.rx_key, cr.tx_key, 32) == 0);

    /* the established channel carries data both ways */
    const char *hm = "shell session payload";
    int hsl = channel_seal(&ci, (const unsigned char *)hm, strlen(hm), sealed, sizeof(sealed));
    int hol = channel_open(&cr, sealed, hsl, opened, sizeof(opened));
    assert(hol == (int)strlen(hm) && memcmp(opened, hm, hol) == 0);

    /* impostor with the wrong identity fails the responder's check */
    assert(channel_hs_finish(&cr, kb.public_key, conf, sizeof(conf)) != 0);

    /* a corrupted RESP signature is rejected by the initiator */
    channel_t cr2, ci2;
    unsigned char init2[CHANNEL_INIT_LEN], resp2[CHANNEL_RESP_LEN], conf2[CHANNEL_CONFIRM_LEN], tmp[32];
    assert(channel_hs_build_init(&ci2, ki.public_key, init2, sizeof(init2)) == CHANNEL_INIT_LEN);
    assert(channel_hs_accept(&cr2, kr.public_key, kr.secret_key, init2, sizeof(init2),
                             tmp, resp2, sizeof(resp2)) == CHANNEL_RESP_LEN);
    resp2[CHANNEL_RESP_LEN - 1] ^= 0x40;  /* flip a byte in resp_sig */
    assert(channel_hs_confirm(&ci2, ki.public_key, ki.secret_key, resp2, sizeof(resp2),
                              tmp, conf2, sizeof(conf2)) == -1);

    /* channel_open_any (BUG-142 endpoint roaming): identify which of N channels a
     * sealed message belongs to, by authenticated open. a.tx -> b.rx; ci.tx -> cr.rx. */
    {
        unsigned char s[512], o[512];
        const char *rm = "roaming probe";
        int rl = channel_seal(&a, (const unsigned char *)rm, strlen(rm), s, sizeof(s));
        assert(rl > 0);
        channel_t *set[] = { &cr, &b, &ci };          /* b (a's peer) is at index 1 */
        assert(channel_open_any(set, 3, s, rl, o, sizeof(o)) == 1);
        assert(memcmp(o, rm, strlen(rm)) == 0);
        /* a set without the matching channel → no match */
        channel_t *none[] = { &cr, &ci };
        assert(channel_open_any(none, 2, s, rl, o, sizeof(o)) == -1);
        /* tampering → no channel authenticates it */
        s[30] ^= 0x10;
        assert(channel_open_any(set, 3, s, rl, o, sizeof(o)) == -1);
    }

    /* ---- external signer (FEAT-028): handshake authenticated without a raw
     * secret in libnorn — the signer callback is the only thing that signs. ---- */
    {
        keypair_t si, sr;
        assert(crypto_keypair_new(&si) == 0);
        assert(crypto_keypair_new(&sr) == 0);
        channel_t ei, er;
        unsigned char ini[CHANNEL_INIT_LEN], rsp[CHANNEL_RESP_LEN],
            cnf[CHANNEL_CONFIRM_LEN], sees_i[32], sees_r[32];

        assert(channel_hs_build_init(&ei, si.public_key, ini, sizeof(ini)) ==
               CHANNEL_INIT_LEN);
        /* responder signs RESP via the external signer */
        assert(channel_hs_accept_signed(&er, sr.public_key, ext_signer,
                                        sr.secret_key, ini, sizeof(ini), sees_i,
                                        rsp, sizeof(rsp)) == CHANNEL_RESP_LEN);
        /* initiator signs CONFIRM via the external signer */
        assert(channel_hs_confirm_signed(&ei, si.public_key, ext_signer,
                                         si.secret_key, rsp, sizeof(rsp), sees_r,
                                         cnf, sizeof(cnf)) == CHANNEL_CONFIRM_LEN);
        /* the signer-authenticated handshake completes and binds true identities */
        assert(channel_hs_finish(&er, sees_i, cnf, sizeof(cnf)) == 0);
        assert(memcmp(sees_i, si.public_key, 32) == 0);
        assert(memcmp(sees_r, sr.public_key, 32) == 0);
        assert(memcmp(ei.tx_key, er.rx_key, 32) == 0);

        /* a signer that fails aborts the responder's RESP */
        channel_t fr;
        unsigned char fin[CHANNEL_INIT_LEN], frsp[CHANNEL_RESP_LEN], ft[32];
        assert(channel_hs_build_init(&ei, si.public_key, fin, sizeof(fin)) ==
               CHANNEL_INIT_LEN);
        assert(channel_hs_accept_signed(&fr, sr.public_key, failing_signer, NULL,
                                        fin, sizeof(fin), ft, frsp,
                                        sizeof(frsp)) == -1);
        /* a signer that fails aborts CONFIRM *after* the RESP verifies (so the
         * sign step is actually reached): build a valid RESP, then confirm with
         * the failing signer. */
        channel_t vi, vr;
        unsigned char vin[CHANNEL_INIT_LEN], vrsp[CHANNEL_RESP_LEN],
            vcnf[CHANNEL_CONFIRM_LEN], vt[32];
        assert(channel_hs_build_init(&vi, si.public_key, vin, sizeof(vin)) ==
               CHANNEL_INIT_LEN);
        assert(channel_hs_accept(&vr, sr.public_key, sr.secret_key, vin,
                                 sizeof(vin), vt, vrsp, sizeof(vrsp)) ==
               CHANNEL_RESP_LEN);
        assert(channel_hs_confirm_signed(&vi, si.public_key, failing_signer, NULL,
                                         vrsp, sizeof(vrsp), vt, vcnf,
                                         sizeof(vcnf)) == -1);

        /* null signer / null secret are rejected by the new and wrapped APIs */
        assert(channel_hs_accept_signed(&er, sr.public_key, NULL, NULL, ini,
                                        sizeof(ini), sees_i, rsp,
                                        sizeof(rsp)) == -1);
        assert(channel_hs_confirm_signed(&ei, si.public_key, NULL, NULL, rsp,
                                         sizeof(rsp), sees_r, cnf,
                                         sizeof(cnf)) == -1);
        assert(channel_hs_accept(&er, sr.public_key, NULL, ini, sizeof(ini),
                                 sees_i, rsp, sizeof(rsp)) == -1);
        assert(channel_hs_confirm(&ei, si.public_key, NULL, rsp, sizeof(rsp),
                                  sees_r, cnf, sizeof(cnf)) == -1);
    }

    printf("test_channel: OK\n");
    return 0;
}
