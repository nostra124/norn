/* SPDX-License-Identifier: MIT */
/* Test idexch build/parse/verify - signed identity exchange protocol. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "idexch.h"
#include "crypto.h"

int main(void) {
    assert(crypto_init() == 0);
    keypair_t k, other;
    assert(crypto_keypair_new(&k) == 0);
    assert(crypto_keypair_new(&other) == 0);

    unsigned char nonce[16]; memset(nonce, 0xab, sizeof(nonce));
    unsigned char ula[16];   memset(ula, 0xfd, sizeof(ula));
    const char *acct = "rene@host.example";
    const char *ver  = "0.8.15";
    const char *payload = "the-value with spaces";

    /* Build and parse REQ with payload */
    unsigned char msg[IDEXCH_MAX];
    int n = idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, ula, ver, acct,
                         (const unsigned char *)payload, strlen(payload), msg, sizeof(msg));
    assert(n > 0);
    assert(idexch_is(msg, (size_t)n));

    unsigned char t, on[16], opub[32], oula[16], opay[IDEXCH_PAYLOAD_MAX];
    size_t opl = 0;
    char over[24], oacct[256];
    assert(idexch_parse(msg, (size_t)n, &t, on, opub, oula, over, sizeof(over),
                        oacct, sizeof(oacct), opay, sizeof(opay), &opl) == 0);
    assert(t == IDEXCH_REQ);
    assert(memcmp(on, nonce, 16) == 0);
    assert(memcmp(opub, k.public_key, 32) == 0);
    assert(memcmp(oula, ula, 16) == 0);
    assert(strcmp(over, ver) == 0);
    assert(strcmp(oacct, acct) == 0);
    assert(opl == strlen(payload) && memcmp(opay, payload, opl) == 0);

    /* Tamper with a signed byte -> reject */
    unsigned char bad[IDEXCH_MAX];
    memcpy(bad, msg, (size_t)n);
    bad[n - 65] ^= 0xff;
    assert(idexch_parse(bad, (size_t)n, &t, on, opub, oula, over, sizeof(over),
                        oacct, sizeof(oacct), opay, sizeof(opay), &opl) == -1);

    /* Sign with one key but claim another's pubkey -> reject */
    memcpy(bad, msg, (size_t)n);
    memcpy(bad + 4 + 1 + 16, other.public_key, 32);
    assert(idexch_parse(bad, (size_t)n, &t, on, opub, oula, over, sizeof(over),
                        oacct, sizeof(oacct), opay, sizeof(opay), &opl) == -1);

    /* Identity-only (no payload) round-trips */
    n = idexch_build(IDEXCH_RESP, nonce, k.public_key, k.secret_key, ula, ver, acct,
                     NULL, 0, msg, sizeof(msg));
    assert(n > 0);
    assert(idexch_parse(msg, (size_t)n, &t, on, opub, oula, over, sizeof(over),
                        oacct, sizeof(oacct), opay, sizeof(opay), &opl) == 0);
    assert(t == IDEXCH_RESP && opl == 0);

    /* BUG-029: payload longer than caller's buffer must be rejected */
    {
        unsigned char big[4 + 1 + 16 + 32 + 16 + 1 + 1 + 2 + 2000 + 64];
        size_t o = 0;
        memcpy(big + o, "NORN", 4); o += 4;
        size_t sstart = o;
        big[o++] = IDEXCH_RESP;
        memset(big + o, 0xab, 16); o += 16;
        memcpy(big + o, k.public_key, 32); o += 32;
        memset(big + o, 0xfd, 16); o += 16;
        big[o++] = 0;
        big[o++] = 0;
        big[o++] = (2000 >> 8) & 0xff;
        big[o++] = 2000 & 0xff;
        memset(big + o, 'x', 2000); o += 2000;
        bf_sign(big + o, big + sstart, o - sstart, k.secret_key); o += 64;
        assert(idexch_parse(big, o, &t, on, opub, oula, over, sizeof(over),
                            oacct, sizeof(oacct), opay, sizeof(opay), &opl) == -1);
    }

    /* Test NULL parameters */
    assert(idexch_build(IDEXCH_REQ, NULL, k.public_key, k.secret_key, ula, ver, acct,
                        NULL, 0, msg, sizeof(msg)) == -1);
    assert(idexch_build(IDEXCH_REQ, nonce, NULL, k.secret_key, ula, ver, acct,
                        NULL, 0, msg, sizeof(msg)) == -1);
    assert(idexch_build(IDEXCH_REQ, nonce, k.public_key, NULL, ula, ver, acct,
                        NULL, 0, msg, sizeof(msg)) == -1);
    assert(idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, NULL, ver, acct,
                        NULL, 0, msg, sizeof(msg)) == -1);
    assert(idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, ula, NULL, acct,
                        NULL, 0, msg, sizeof(msg)) == -1);
    assert(idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, ula, ver, NULL,
                        NULL, 0, msg, sizeof(msg)) == -1);
    assert(idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, ula, ver, acct,
                        NULL, 0, NULL, sizeof(msg)) == -1);

    /* paylen > 0 with NULL payload -> reject (line 19 first arm true) */
    assert(idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, ula, ver, acct,
                        NULL, 5, msg, sizeof(msg)) == -1);

    /* Oversized version / account / payload fields (line 21, each disjunct) */
    {
        char longstr[300];
        memset(longstr, 'a', sizeof(longstr));
        longstr[sizeof(longstr) - 1] = '\0';  /* 299 chars > 255 */
        /* version too long */
        assert(idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, ula,
                            longstr, acct, NULL, 0, msg, sizeof(msg)) == -1);
        /* account too long */
        assert(idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, ula,
                            ver, longstr, NULL, 0, msg, sizeof(msg)) == -1);
    }
    /* payload too long */
    {
        unsigned char bigpay[IDEXCH_PAYLOAD_MAX + 1];
        memset(bigpay, 'p', sizeof(bigpay));
        assert(idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, ula, ver,
                            acct, bigpay, sizeof(bigpay), msg, sizeof(msg)) == -1);
    }

    /* outcap too small -> reject (line 23) */
    assert(idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, ula, ver, acct,
                        NULL, 0, msg, 8) == -1);

    /* idexch_parse on a buffer that fails idexch_is (line 46) */
    assert(idexch_parse((unsigned char *)"XXXXX", 5, &t, on, opub, oula, over,
                        sizeof(over), oacct, sizeof(oacct), opay, sizeof(opay), &opl) == -1);

    /* Valid magic but too short for the fixed header (line 49) */
    {
        unsigned char shortmsg[10];
        memcpy(shortmsg, "NORN", 4);
        shortmsg[4] = IDEXCH_REQ;  /* only 6 bytes total, header needs >= 4+66 */
        assert(idexch_parse(shortmsg, 6, &t, on, opub, oula, over, sizeof(over),
                            oacct, sizeof(oacct), opay, sizeof(opay), &opl) == -1);
    }

    /* vlen overruns the buffer (line 58) */
    {
        unsigned char m[4 + 1 + 16 + 32 + 16 + 1];
        size_t o = 0;
        memcpy(m + o, "NORN", 4); o += 4;
        m[o++] = IDEXCH_REQ;
        memset(m + o, 0, 16); o += 16;
        memset(m + o, 0, 32); o += 32;
        memset(m + o, 0, 16); o += 16;
        m[o++] = 200;  /* claims 200-byte version, but nothing follows */
        assert(idexch_parse(m, o, &t, on, opub, oula, over, sizeof(over),
                            oacct, sizeof(oacct), opay, sizeof(opay), &opl) == -1);
    }

    /* alen overruns the buffer (line 61) */
    {
        unsigned char m[4 + 1 + 16 + 32 + 16 + 1 + 1];
        size_t o = 0;
        memcpy(m + o, "NORN", 4); o += 4;
        m[o++] = IDEXCH_REQ;
        memset(m + o, 0, 16); o += 16;
        memset(m + o, 0, 32); o += 32;
        memset(m + o, 0, 16); o += 16;
        m[o++] = 0;    /* vlen = 0 */
        m[o++] = 200;  /* alen = 200, but nothing follows */
        assert(idexch_parse(m, o, &t, on, opub, oula, over, sizeof(over),
                            oacct, sizeof(oacct), opay, sizeof(opay), &opl) == -1);
    }

    /* plen + SIG_LEN overruns the buffer (line 64) */
    {
        unsigned char m[4 + 1 + 16 + 32 + 16 + 1 + 1 + 2];
        size_t o = 0;
        memcpy(m + o, "NORN", 4); o += 4;
        m[o++] = IDEXCH_REQ;
        memset(m + o, 0, 16); o += 16;
        memset(m + o, 0, 32); o += 32;
        memset(m + o, 0, 16); o += 16;
        m[o++] = 0;    /* vlen = 0 */
        m[o++] = 0;    /* alen = 0 */
        m[o++] = 0;    /* plen hi */
        m[o++] = 10;   /* plen = 10, but no payload + sig follow */
        assert(idexch_parse(m, o, &t, on, opub, oula, over, sizeof(over),
                            oacct, sizeof(oacct), opay, sizeof(opay), &opl) == -1);
    }

    /* line 68 first arm false: payload==NULL skips the plen>paycap guard. Parse a
     * well-formed message with ALL out params NULL (also exercises the false arm of
     * every line 74-81 `if (out)` guard) and it must still succeed. */
    n = idexch_build(IDEXCH_REQ, nonce, k.public_key, k.secret_key, ula, ver, acct,
                     (const unsigned char *)payload, strlen(payload), msg, sizeof(msg));
    assert(n > 0);
    assert(idexch_parse(msg, (size_t)n, NULL, NULL, NULL, NULL, NULL, 0,
                        NULL, 0, NULL, 0, NULL) == 0);

    /* line 68 middle arm false: payload non-NULL but paycap 0 -> guard skipped, OK.
     * Also covers lines 78/79 second-arm-false: version/account non-NULL but
     * vcap/acap == 0, so the `&& vcap` / `&& acap` short-circuits to false. */
    assert(idexch_parse(msg, (size_t)n, &t, on, opub, oula, over, 0,
                        oacct, 0, opay, 0, &opl) == 0);

    /* lines 78/79: version/account out buffers smaller than the field -> truncate
     * (false arm of `vlen < vcap-1` / `alen < acap-1`). */
    {
        char tinyver[3], tinyacct[3];
        assert(idexch_parse(msg, (size_t)n, &t, on, opub, oula, tinyver, sizeof(tinyver),
                            tinyacct, sizeof(tinyacct), opay, sizeof(opay), &opl) == 0);
        assert(strlen(tinyver) == 2 && strlen(tinyacct) == 2);
    }

    /* line 81 false arm of `plen < paycap`: paycap == plen, copies paycap bytes. */
    {
        const char *sp = "abc";
        int sn = idexch_build(IDEXCH_RESP, nonce, k.public_key, k.secret_key, ula, ver,
                              acct, (const unsigned char *)sp, 3, msg, sizeof(msg));
        assert(sn > 0);
        unsigned char tinypay[3];  /* paycap == plen == 3, so plen < paycap is false */
        size_t tpl = 0;
        assert(idexch_parse(msg, (size_t)sn, &t, on, opub, oula, over, sizeof(over),
                            oacct, sizeof(oacct), tinypay, sizeof(tinypay), &tpl) == 0);
        assert(tpl == 3 && memcmp(tinypay, sp, 3) == 0);
    }

    /* Test idexch_is with invalid inputs */
    assert(idexch_is(NULL, 0) == 0);
    assert(idexch_is((unsigned char*)"NORN", 4) == 0);  /* too short */
    assert(idexch_is((unsigned char*)"XXXX", 5) == 0);  /* wrong magic byte 0 */
    assert(idexch_is((unsigned char*)"BXID\x01", 5) == 0);  /* wrong magic byte 1 */
    assert(idexch_is((unsigned char*)"BFXD\x01", 5) == 0);  /* wrong magic byte 2 */
    assert(idexch_is((unsigned char*)"BFIX\x01", 5) == 0);  /* wrong magic byte 3 */
    assert(idexch_is((unsigned char*)"NORN\x01", 5) == 1);  /* valid */

    printf("test_idexch: OK\n");
    return 0;
}