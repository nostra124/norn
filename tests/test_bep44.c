/* Test BEP-44 encoding/decoding (copied from bifrost test_bep44.c) */
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
    unsigned char vout[1024];
    uint32_t seq = 1;
    
    assert(sodium_init() >= 0);
    
    /* Generate keypair */
    crypto_sign_keypair(pk, sk);
    
    /* Compute target = sha1("k" || pk) */
    bep44_target_for_pubkey(target, pk);
    
    /* Encode mutable */
    int len = bep44_encode(out, sizeof(out), pk, value, sizeof(value) - 1, seq, sk);
    assert(len > 0);
    
    /* Decode */
    unsigned char pk2[32], vout2[1024];
    size_t vlen;
    uint32_t seq2;
    assert(bep44_decode(out, len, pk2, vout2, &vlen, &seq2) == 0);
    assert(memcmp(pk2, pk, 32) == 0);
    assert(vlen == sizeof(value) - 1);
    assert(memcmp(vout2, value, vlen) == 0);
    assert(seq2 == seq);
    
    printf("test_bep44: OK\n");
    return 0;
}