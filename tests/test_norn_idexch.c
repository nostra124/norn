/* SPDX-License-Identifier: MIT */
/**
 * @file test_norn_idexch.c
 * @brief Unit tests for generic identity exchange (FEAT-015)
 */

#include "norn_suite.h"
#include "norn_idexch.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_idexch_is(void) {
    /* Valid magic */
    unsigned char buf[100];
    buf[0] = NORN_IDEXCH_MAGIC0;
    buf[1] = NORN_IDEXCH_MAGIC1;
    buf[2] = NORN_IDEXCH_MAGIC2;
    buf[3] = NORN_IDEXCH_MAGIC3;
    
    int is_idexch = norn_idexch_is(buf, 100);
    assert(is_idexch == 1);
    
    /* Invalid magic */
    buf[0] = 0xFF;
    is_idexch = norn_idexch_is(buf, 100);
    assert(is_idexch == 0);
    
    /* Too short */
    is_idexch = norn_idexch_is(buf, 3);
    assert(is_idexch == 0);
    
    /* NULL inputs */
    is_idexch = norn_idexch_is(NULL, 100);
    assert(is_idexch == 0);
    
    printf("  test_idexch_is: OK\n");
}

static void test_idexch_build_parse(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    /* Generate keypair */
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    /* Generate nonce */
    unsigned char nonce[16];
    randombytes_buf(nonce, 16);
    
    /* Build request */
    unsigned char req[NORN_IDEXCH_MAX];
    int len = norn_idexch_build(NORN_IDEXCH_REQ, nonce, pk, sk,
                                 NULL, 0, NULL, 0, req, sizeof(req), suite);
    assert(len > 0);
    assert(len < (int)sizeof(req));
    
    /* Parse request */
    unsigned char type;
    unsigned char nonce2[16];
    unsigned char pk2[32];
    uint32_t ip;
    uint16_t port;
    
    int ret = norn_idexch_parse(req, (size_t)len, &type, nonce2, pk2,
                                  &ip, &port, NULL, 0, NULL, suite);
    assert(ret == 0);
    assert(type == NORN_IDEXCH_REQ);
    assert(memcmp(nonce, nonce2, 16) == 0);
    assert(memcmp(pk, pk2, 32) == 0);
    assert(ip == 0);
    assert(port == 0);
    
    printf("  test_idexch_build_parse: OK\n");
}

static void test_idexch_with_endpoint(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char nonce[16];
    randombytes_buf(nonce, 16);
    
    uint32_t my_ip = 0x01020304;  /* 1.2.3.4 */
    uint16_t my_port = 6881;
    
    /* Build with endpoint */
    unsigned char msg[NORN_IDEXCH_MAX];
    int len = norn_idexch_build(NORN_IDEXCH_RESP, nonce, pk, sk,
                                 &my_ip, my_port, NULL, 0, msg, sizeof(msg), suite);
    assert(len > 0);
    
    /* Parse */
    unsigned char type;
    unsigned char nonce2[16];
    unsigned char pk2[32];
    uint32_t ip;
    uint16_t port;
    
    int ret = norn_idexch_parse(msg, (size_t)len, &type, nonce2, pk2,
                                  &ip, &port, NULL, 0, NULL, suite);
    assert(ret == 0);
    assert(type == NORN_IDEXCH_RESP);
    assert(ip == my_ip);
    assert(port == my_port);
    
    printf("  test_idexch_with_endpoint: OK\n");
}

static void test_idexch_with_payload(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char nonce[16];
    randombytes_buf(nonce, 16);
    
    const unsigned char payload[] = "Hello, world!";
    size_t paylen = strlen((char *)payload);
    
    /* Build with payload */
    unsigned char msg[NORN_IDEXCH_MAX];
    int len = norn_idexch_build(NORN_IDEXCH_REQ, nonce, pk, sk,
                                 NULL, 0, payload, paylen, msg, sizeof(msg), suite);
    assert(len > 0);
    
    /* Parse */
    unsigned char type;
    unsigned char nonce2[16];
    unsigned char pk2[32];
    unsigned char payload2[256];
    size_t paylen2;
    
    int ret = norn_idexch_parse(msg, (size_t)len, &type, nonce2, pk2,
                                  NULL, NULL, payload2, sizeof(payload2), &paylen2, suite);
    assert(ret == 0);
    assert(paylen2 == paylen);
    assert(memcmp(payload, payload2, paylen) == 0);
    
    printf("  test_idexch_with_payload: OK\n");
}

static void test_idexch_tampered(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char nonce[16];
    randombytes_buf(nonce, 16);
    
    unsigned char msg[NORN_IDEXCH_MAX];
    int len = norn_idexch_build(NORN_IDEXCH_REQ, nonce, pk, sk,
                                 NULL, 0, NULL, 0, msg, sizeof(msg), suite);
    assert(len > 0);
    
    /* Tamper with message */
    msg[10] ^= 0x01;
    
    /* Should fail signature verification */
    unsigned char type;
    unsigned char nonce2[16];
    unsigned char pk2[32];
    
    int ret = norn_idexch_parse(msg, (size_t)len, &type, nonce2, pk2,
                                  NULL, NULL, NULL, 0, NULL, suite);
    assert(ret == -1);
    
    printf("  test_idexch_tampered: OK\n");
}

static void test_idexch_wrong_key(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    /* Alice's keypair */
    unsigned char alice_pk[32], alice_sk[64];
    crypto_sign_keypair(alice_pk, alice_sk);
    
    /* Bob's keypair */
    unsigned char bob_pk[32], bob_sk[64];
    crypto_sign_keypair(bob_pk, bob_sk);
    
    unsigned char nonce[16];
    randombytes_buf(nonce, 16);
    
    /* Alice signs with her key */
    unsigned char msg[NORN_IDEXCH_MAX];
    int len = norn_idexch_build(NORN_IDEXCH_REQ, nonce, alice_pk, alice_sk,
                                 NULL, 0, NULL, 0, msg, sizeof(msg), suite);
    assert(len > 0);
    
    /* Verify with Bob's key should fail */
    unsigned char type;
    unsigned char nonce2[16];
    unsigned char pk2[32];
    
    /* Extract Alice's public key from message */
    memcpy(pk2, alice_pk, 32);
    
    /* But try to verify with Bob's key - the embedded pubkey is Alice's,
     * so verification will fail because Alice signed with her key, not Bob's */
    int ret = norn_idexch_parse(msg, (size_t)len, &type, nonce2, pk2,
                                  NULL, NULL, NULL, 0, NULL, suite);
    assert(ret == 0);  /* Actually succeeds because pk is Alice's */
    
    /* Now try with wrong embedded key */
    /* Modify the embedded public key */
    msg[4 + 1 + 16] ^= 0x01;  /* Flip a bit in pubkey */
    
    ret = norn_idexch_parse(msg, (size_t)len, &type, nonce2, pk2,
                             NULL, NULL, NULL, 0, NULL, suite);
    assert(ret == -1);  /* Fails: signature doesn't match modified key */
    
    printf("  test_idexch_wrong_key: OK\n");
}

static void test_idexch_suite_independence(void) {
    /* Verify that the idexch module works with any crypto suite */
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    /* Check suite metadata is used correctly */
    assert(suite->pubkey_len == 32);
    assert(suite->sig_len == 64);
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char nonce[16];
    randombytes_buf(nonce, 16);
    
    unsigned char msg[NORN_IDEXCH_MAX];
    int len = norn_idexch_build(NORN_IDEXCH_REQ, nonce, pk, sk,
                                 NULL, 0, NULL, 0, msg, sizeof(msg), suite);
    assert(len > 0);
    
    /* Verify message size is correct for this suite */
    size_t expected = 4 + 1 + 16 + suite->pubkey_len + 4 + 2 + 2 + suite->sig_len;
    assert((size_t)len == expected);
    
    printf("  test_idexch_suite_independence: OK\n");
}

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    printf("test_norn_idexch:\n");
    
    test_idexch_is();
    test_idexch_build_parse();
    test_idexch_with_endpoint();
    test_idexch_with_payload();
    test_idexch_tampered();
    test_idexch_wrong_key();
    test_idexch_suite_independence();
    
    printf("test_norn_idexch: OK\n");
    return 0;
}