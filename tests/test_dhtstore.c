/* Test DHT storage */
#include "dhtstore.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    unsigned char value[] = "test value";
    unsigned char out[1024];
    unsigned char k_out[32];
    uint32_t seq_out;
    size_t outlen;
    unsigned char sig_out[64];
    
    /* Initialize with 2MB budget */
    size_t budget = dhtstore_init(2, 0);
    printf("DHT store initialized with %zu bytes\n", budget);
    assert(budget > 0);
    
    /* Put immutable value */
    unsigned char target[20];
    assert(dhtstore_put_immutable(value, sizeof(value) - 1, 0, target) == 1);
    
    /* Get it back */
    assert(dhtstore_get(target, k_out, &seq_out, out, sizeof(out), &outlen, sig_out) == 1);
    assert(outlen == sizeof(value) - 1);
    assert(memcmp(out, value, outlen) == 0);
    
    printf("test_dhtstore: OK\n");
    return 0;
}