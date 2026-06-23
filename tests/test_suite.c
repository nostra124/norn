/**
 * @file test_suite.c
 * @brief Unit tests for norn_crypto_suite_t (FEAT-013)
 */

#include "norn_suite.h"
#include "crypto.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_suite_sodium_metadata(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    assert(suite != NULL);
    assert(suite->name != NULL);
    assert(strcmp(suite->name, "sodium") == 0);
    
    assert(suite->pubkey_len == 32);
    assert(suite->secret_len == 64);
    assert(suite->sig_len == 64);
    assert(suite->nodeid_len == 32);
    assert(suite->eph_pubkey_len == 32);
    assert(suite->eph_secret_len == 32);
    assert(suite->shared_len == 32);
    assert(suite->aead_key_len == 32);
    assert(suite->aead_nonce_len == 24);
    assert(suite->aead_overhead == 16);
    
    printf("  test_suite_sodium_metadata: OK\n");
}

static void test_suite_sodium_sign_verify(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    const unsigned char msg[] = "Hello, world!";
    size_t msg_len = strlen((char *)msg);
    
    unsigned char sig[64];
    int ret = suite->sign(sig, msg, msg_len, sk);
    assert(ret == 0);
    
    ret = suite->verify(sig, msg, msg_len, pk);
    assert(ret == 0);
    
    /* Tampered message should fail verification */
    unsigned char msg2[32];
    memcpy(msg2, msg, msg_len);
    msg2[0] ^= 0x01;
    ret = suite->verify(sig, msg2, msg_len, pk);
    assert(ret == -1);
    
    /* NULL inputs */
    ret = suite->sign(NULL, msg, msg_len, sk);
    assert(ret == -1);
    ret = suite->sign(sig, NULL, msg_len, sk);
    assert(ret == -1);
    
    printf("  test_suite_sodium_sign_verify: OK\n");
}

static void test_suite_sodium_eph_keygen(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char eph_pk[32], eph_sk[32];
    
    int ret = suite->eph_keygen(eph_pk, eph_sk);
    assert(ret == 0);
    
    /* Generated keys should be valid (non-zero) */
    int all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (eph_pk[i] != 0) { all_zero = 0; break; }
    }
    assert(!all_zero);
    
    all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (eph_sk[i] != 0) { all_zero = 0; break; }
    }
    assert(!all_zero);
    
    /* NULL inputs */
    ret = suite->eph_keygen(NULL, eph_sk);
    assert(ret == -1);
    ret = suite->eph_keygen(eph_pk, NULL);
    assert(ret == -1);
    
    printf("  test_suite_sodium_eph_keygen: OK\n");
}

static void test_suite_sodium_ecdh(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    /* Generate two keypairs */
    unsigned char alice_pk[32], alice_sk[32];
    unsigned char bob_pk[32], bob_sk[32];
    suite->eph_keygen(alice_pk, alice_sk);
    suite->eph_keygen(bob_pk, bob_sk);
    
    /* ECDH */
    unsigned char alice_shared[32], bob_shared[32];
    int ret = suite->ecdh(alice_shared, alice_sk, bob_pk);
    assert(ret == 0);
    
    ret = suite->ecdh(bob_shared, bob_sk, alice_pk);
    assert(ret == 0);
    
    /* Both should derive the same shared secret */
    assert(memcmp(alice_shared, bob_shared, 32) == 0);
    
    /* NULL inputs */
    ret = suite->ecdh(NULL, alice_sk, bob_pk);
    assert(ret == -1);
    
    printf("  test_suite_sodium_ecdh: OK\n");
}

static void test_suite_sodium_aead(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char key[32], nonce[24];
    memset(key, 0x42, 32);
    memset(nonce, 0x24, 24);
    
    const unsigned char pt[] = "Secret message";
    size_t pt_len = strlen((char *)pt);
    
    unsigned char ct[128];
    size_t ct_len;
    
    int ret = suite->aead_seal(ct, &ct_len, pt, pt_len, key, nonce);
    assert(ret == 0);
    assert(ct_len == pt_len + suite->aead_overhead);
    
    unsigned char pt2[128];
    size_t pt2_len;
    
    ret = suite->aead_open(pt2, &pt2_len, ct, ct_len, key, nonce);
    assert(ret == 0);
    assert(pt2_len == pt_len);
    assert(memcmp(pt, pt2, pt_len) == 0);
    
    /* Tampered ciphertext should fail */
    ct[0] ^= 0x01;
    ret = suite->aead_open(pt2, &pt2_len, ct, ct_len, key, nonce);
    assert(ret == -1);
    
    /* Wrong key should fail */
    unsigned char wrong_key[32];
    memset(wrong_key, 0x99, 32);
    ct[0] ^= 0x01;  /* Restore */
    ret = suite->aead_open(pt2, &pt2_len, ct, ct_len, wrong_key, nonce);
    assert(ret == -1);
    
    printf("  test_suite_sodium_aead: OK\n");
}

static void test_suite_sodium_nodeid_from_pubkey(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    unsigned char pk[32];
    memset(pk, 0xAB, 32);
    
    unsigned char nodeid[32];
    int ret = suite->nodeid_from_pubkey(nodeid, pk);
    assert(ret == 0);
    
    /* Same input → same output */
    unsigned char nodeid2[32];
    ret = suite->nodeid_from_pubkey(nodeid2, pk);
    assert(ret == 0);
    assert(memcmp(nodeid, nodeid2, 32) == 0);
    
    /* Different input → different output */
    pk[0] ^= 0x01;
    ret = suite->nodeid_from_pubkey(nodeid2, pk);
    assert(ret == 0);
    assert(memcmp(nodeid, nodeid2, 32) != 0);
    
    /* NULL inputs */
    ret = suite->nodeid_from_pubkey(NULL, pk);
    assert(ret == -1);
    ret = suite->nodeid_from_pubkey(nodeid, NULL);
    assert(ret == -1);
    
    printf("  test_suite_sodium_nodeid_from_pubkey: OK\n");
}

static void test_suite_sodium_hash(void) {
    const norn_crypto_suite_t *suite = norn_suite_sodium();
    
    const unsigned char data[] = "Hello, world!";
    size_t data_len = strlen((char *)data);
    
    unsigned char hash[32];
    int ret = suite->hash(hash, data, data_len);
    assert(ret == 0);
    
    /* Verify it's SHA-256 */
    unsigned char expected[32];
    crypto_hash_sha256(expected, data, data_len);
    assert(memcmp(hash, expected, 32) == 0);
    
    /* NULL inputs */
    ret = suite->hash(NULL, data, data_len);
    assert(ret == -1);
    ret = suite->hash(hash, NULL, data_len);
    assert(ret == -1);
    
    printf("  test_suite_sodium_hash: OK\n");
}

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    printf("test_suite:\n");
    
    test_suite_sodium_metadata();
    test_suite_sodium_sign_verify();
    test_suite_sodium_eph_keygen();
    test_suite_sodium_ecdh();
    test_suite_sodium_aead();
    test_suite_sodium_nodeid_from_pubkey();
    test_suite_sodium_hash();
    
    printf("test_suite: OK\n");
    return 0;
}