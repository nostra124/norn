/* norn — Mainline DHT client CLI */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>
#include "libnorn/norn.h"

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <command> [args...]\n", prog);
    fprintf(stderr, "\nCommands:\n");
    fprintf(stderr, "  set <key> <value>    Store a mutable signed record\n");
    fprintf(stderr, "  get <key>            Retrieve a record\n");
    fprintf(stderr, "  daemon               Run as a DHT daemon\n");
    fprintf(stderr, "  version              Print version\n");
    exit(1);
}

static int do_version(void) {
    printf("norn 0.1.0\n");
    return 0;
}

static int do_get(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s get <key>\n", argv[0]);
        return 1;
    }
    
    const char *key_str = argv[2];
    unsigned char key[NORN_ID_BYTES];
    
    /* Key should be hex or base32 */
    size_t keylen = strlen(key_str);
    if (keylen != NORN_ID_BYTES * 2) {
        fprintf(stderr, "Key must be %d hex characters\n", NORN_ID_BYTES * 2);
        return 1;
    }
    
    /* Parse hex key */
    for (int i = 0; i < NORN_ID_BYTES; i++) {
        unsigned int byte;
        if (sscanf(key_str + i*2, "%2x", &byte) != 1) {
            fprintf(stderr, "Invalid hex in key at position %d\n", i*2);
            return 1;
        }
        key[i] = (unsigned char)byte;
    }
    
    /* For now, print key - actual DHT get requires running client */
    printf("Key: ");
    for (int i = 0; i < NORN_ID_BYTES; i++) printf("%02x", key[i]);
    printf("\n");
    printf("(DHT lookup not implemented in this example - need running client)\n");
    
    return 0;
}

static int do_set(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s set <key> <value>\n", argv[0]);
        return 1;
    }
    
    const char *key_str = argv[2];
    const char *value = argv[3];
    
    printf("Key: %s\n", key_str);
    printf("Value: %s\n", value);
    printf("(DHT put not implemented in this example - need running client)\n");
    
    return 0;
}

static int do_daemon(int argc, char **argv) {
    printf("Starting DHT daemon...\n");
    printf("(Daemon mode not implemented in this example)\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
    }
    
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    const char *cmd = argv[1];
    
    if (strcmp(cmd, "version") == 0) return do_version();
    if (strcmp(cmd, "get") == 0) return do_get(argc, argv);
    if (strcmp(cmd, "set") == 0) return do_set(argc, argv);
    if (strcmp(cmd, "daemon") == 0) return do_daemon(argc, argv);
    
    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
    return 1;
}