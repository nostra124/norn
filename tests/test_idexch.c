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
        memcpy(big + o, "BFID", 4); o += 4;
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

    /* Test idexch_is with invalid inputs */
    assert(idexch_is(NULL, 0) == 0);
    assert(idexch_is((unsigned char*)"BFID", 4) == 0);  /* too short */
    assert(idexch_is((unsigned char*)"XXXX", 5) == 0);  /* wrong magic */
    assert(idexch_is((unsigned char*)"BFID\x01", 5) == 1);  /* valid */

    printf("test_idexch: OK\n");
    return 0;
}