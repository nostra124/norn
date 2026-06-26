/* Unit tests for private-overlay config (FEAT-020), norn_overlay.
 * Pure module — exercised to 100% line + branch coverage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "norn_overlay.h"

static void mkpk(unsigned char *pk, int i) {
    memset(pk, 0, NORN_PUBKEY_BYTES);
    pk[0] = (unsigned char)(i + 1);
    pk[31] = (unsigned char)(0x80 + i);
}

static void test_add_and_query(void) {
    norn_overlay_t *ov = norn_overlay_new();
    assert(ov);
    assert(norn_overlay_peer_count(ov) == 0);

    unsigned char a[32], b[32];
    mkpk(a, 0);
    mkpk(b, 1);
    assert(norn_overlay_add_peer(ov, a, 0x0a000001, 0x1f90) == 0);
    assert(norn_overlay_add_peer(ov, b, 0x0a000002, 0x1f91) == 0);
    assert(norn_overlay_peer_count(ov) == 2);

    norn_overlay_peer_t out;
    assert(norn_overlay_find(ov, a, &out) == 0);
    assert(out.ip == 0x0a000001 && out.port == 0x1f90);
    assert(norn_overlay_find(ov, b, NULL) == 0); /* NULL out tolerated */
    unsigned char miss[32];
    mkpk(miss, 9);
    assert(norn_overlay_find(ov, miss, &out) == -1);

    /* duplicate / invalid rejected */
    assert(norn_overlay_add_peer(ov, a, 0x0a000009, 0x1234) == -1); /* dup */
    unsigned char zero[32] = {0};
    assert(norn_overlay_add_peer(ov, zero, 0x0a000003, 0x10) == -1); /* zero key */
    assert(norn_overlay_add_peer(ov, miss, 0, 0x10) == -1);          /* zero ip */
    assert(norn_overlay_add_peer(ov, miss, 0x0a000003, 0) == -1);    /* zero port */
    norn_overlay_free(ov);
}

static void test_to_config(void) {
    norn_overlay_t *ov = norn_overlay_new();
    unsigned char a[32], b[32];
    mkpk(a, 0);
    mkpk(b, 1);
    norn_overlay_add_peer(ov, a, 0x0a000001, 0x1f90);
    norn_overlay_add_peer(ov, b, 0x0a000002, 0x1f91);

    norn_config_t cfg;
    uint32_t ips[4];
    uint16_t ports[4];
    int n = norn_overlay_to_config(ov, "fleet/1.0", &cfg, ips, ports, 4);
    assert(n == 2);
    assert(cfg.private_mode == 1);
    assert(cfg.boot_count == 2);
    assert(strcmp(cfg.version, "fleet/1.0") == 0);
    assert(cfg.boot_ips == ips && cfg.boot_ports == ports);
    assert(ips[0] == 0x0a000001 && ports[1] == 0x1f91);

    /* pubkeys projection (e.g. to seed cluster membership). */
    unsigned char keys[4][32];
    assert(norn_overlay_pubkeys(ov, keys, 4) == 2);
    assert(memcmp(keys[0], a, 32) == 0 && memcmp(keys[1], b, 32) == 0);

    /* capacity too small */
    assert(norn_overlay_to_config(ov, "v", &cfg, ips, ports, 1) == -1);
    assert(norn_overlay_pubkeys(ov, keys, 1) == -1);

    /* version may be NULL */
    assert(norn_overlay_to_config(ov, NULL, &cfg, ips, ports, 4) == 2);
    assert(cfg.version == NULL);
    norn_overlay_free(ov);
}

static void test_full_table(void) {
    norn_overlay_t *ov = norn_overlay_new();
    for (int i = 0; i < NORN_OVERLAY_MAX_PEERS; i++) {
        unsigned char pk[32];
        mkpk(pk, i);
        assert(norn_overlay_add_peer(ov, pk, 0x0a000000u + (uint32_t)i + 1, (uint16_t)(i + 1)) == 0);
    }
    unsigned char extra[32];
    mkpk(extra, 200);
    assert(norn_overlay_add_peer(ov, extra, 0x0b000001, 0x20) == -1); /* full */
    assert(norn_overlay_peer_count(ov) == NORN_OVERLAY_MAX_PEERS);
    norn_overlay_free(ov);
}

static void test_empty_and_null(void) {
    /* empty overlay cannot produce a config. */
    norn_overlay_t *ov = norn_overlay_new();
    norn_config_t cfg;
    uint32_t ips[2];
    uint16_t ports[2];
    assert(norn_overlay_to_config(ov, "v", &cfg, ips, ports, 2) == -1);
    unsigned char keys[2][32];
    assert(norn_overlay_pubkeys(ov, keys, 2) == 0); /* zero peers → 0 copied */
    norn_overlay_free(ov);

    unsigned char pk[32];
    mkpk(pk, 0);
    assert(norn_overlay_add_peer(NULL, pk, 1, 1) == -1);
    assert(norn_overlay_add_peer(ov = norn_overlay_new(), NULL, 1, 1) == -1);
    assert(norn_overlay_peer_count(NULL) == -1);
    assert(norn_overlay_find(NULL, pk, NULL) == -1);
    assert(norn_overlay_find(ov, NULL, NULL) == -1);
    assert(norn_overlay_to_config(NULL, "v", &cfg, ips, ports, 2) == -1);
    assert(norn_overlay_to_config(ov, "v", NULL, ips, ports, 2) == -1);
    assert(norn_overlay_to_config(ov, "v", &cfg, NULL, ports, 2) == -1);
    assert(norn_overlay_to_config(ov, "v", &cfg, ips, NULL, 2) == -1);
    assert(norn_overlay_pubkeys(NULL, keys, 2) == -1);
    assert(norn_overlay_pubkeys(ov, NULL, 2) == -1);
    norn_overlay_free(ov);
    norn_overlay_free(NULL);
}

int main(void) {
    test_add_and_query();
    test_to_config();
    test_full_table();
    test_empty_and_null();
    printf("test_overlay: all passed\n");
    return 0;
}
