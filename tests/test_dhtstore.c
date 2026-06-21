/* Test DHT storage (copied from bifrost test_dhtstore.c) */
#include "dhtstore.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    dhtstore_t store;
    assert(dhtstore_init(&store, "/tmp/test_dhtstore.db") == 0);
    
    unsigned char key[20] = {0};
    unsigned char value[] = "test value";
    
    /* Put and get */
    assert(dhtstore_put(&store, key, value, sizeof(value) - 1) == 0);
    
    unsigned char out[1024];
    size_t outlen;
    assert(dhtstore_get(&store, key, out, sizeof(out), &outlen) == 0);
    assert(outlen == sizeof(value) - 1);
    assert(memcmp(out, value, outlen) == 0);
    
    dhtstore_close(&store);
    
    printf("test_dhtstore: OK\n");
    return 0;
}