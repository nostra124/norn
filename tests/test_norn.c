/* norn stub test */
#include "norn.h"
#include <sodium.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    unsigned char pubkey[NORN_PUBKEY_BYTES];
    unsigned char seckey[NORN_SECRETKEY_BYTES];
    
    /* Generate a keypair */
    crypto_sign_keypair(pubkey, seckey);
    
    /* Create client with default config */
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test";
    
    norn_client_t *client = norn_new(pubkey, seckey, &cfg);
    assert(client != NULL);
    
    /* Get node ID */
    unsigned char id[NORN_ID_BYTES];
    assert(norn_get_id(client, id) == 0);
    
    printf("Client created, node ID: ");
    for (int i = 0; i < NORN_ID_BYTES; i++) printf("%02x", id[i]);
    printf("\n");
    
    norn_free(client);
    printf("test_norn: OK\n");
    return 0;
}