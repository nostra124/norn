/* Test BEP-44 encoding/decoding */
#include "bep44.h"
#include "sha1.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sodium.h>

int main(void) {
    unsigned char pk[32], sk[64];
    unsigned char target[20];
    unsigned char value[] = "hello world";
    unsigned char out[1024];
    uint32_t seq = 1;
    
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    /* Generate keypair */
    crypto_sign_keypair(pk, sk);
    
    /* Compute target = sha1("k" || pk) */
    bep44_target_for_pubkey(target, pk);
    
    /* Verify target is computed correctly */
    printf("Target: ");
    for (int i = 0; i < 20; i++) printf("%02x", target[i]);
    printf("\n");
    
    /* Test encode */
    int len = bep44_encode(out, sizeof(out), pk, value, sizeof(value) - 1, seq, sk);
    assert(len > 0);
    printf("Encoded length: %d\n", len);
    
    /* Test decode */
    unsigned char pk2[32], sig[64];
    unsigned char *vout;
    size_t vlen;
    uint32_t seq2;
    
    int ret = bep44_decode(out, (size_t)len, pk2, &vout, &vlen, &seq2, sig);
    assert(ret == 0);
    assert(memcmp(pk2, pk, 32) == 0);
    assert(vlen == sizeof(value) - 1);
    assert(memcmp(vout, value, vlen) == 0);
    assert(seq2 == seq);
    
    printf("test_bep44: OK\n");
    return 0;
}