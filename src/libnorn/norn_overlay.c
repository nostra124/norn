/* SPDX-License-Identifier: MIT */
/**
 * @file norn_overlay.c
 * @brief Private-overlay bootstrap configuration (FEAT-020). See norn_overlay.h.
 *
 * Pure and deterministic: build/validate the fleet bootstrap peer set and
 * project it into a private-mode norn_config_t. No I/O, no globals. Exercised
 * to full line/branch coverage in isolation.
 */

#include "norn_overlay.h"

#include <stdlib.h>
#include <string.h>

struct norn_overlay {
    norn_overlay_peer_t peers[NORN_OVERLAY_MAX_PEERS];
    int n;
};

static int is_zero_key(const unsigned char *pk) {
    unsigned char acc = 0;
    for (int i = 0; i < NORN_PUBKEY_BYTES; i++) acc |= pk[i];
    return acc == 0;
}

static const norn_overlay_peer_t *find_peer(const norn_overlay_t *ov,
                                            const unsigned char *pk) {
    for (int i = 0; i < ov->n; i++)
        if (memcmp(ov->peers[i].pubkey, pk, NORN_PUBKEY_BYTES) == 0)
            return &ov->peers[i];
    return NULL;
}

norn_overlay_t *norn_overlay_new(void) {
    return calloc(1, sizeof(norn_overlay_t));
}

void norn_overlay_free(norn_overlay_t *ov) { free(ov); }

int norn_overlay_add_peer(norn_overlay_t *ov,
                          const unsigned char pubkey[NORN_PUBKEY_BYTES],
                          uint32_t ip, uint16_t port) {
    if (!ov || !pubkey || ip == 0 || port == 0) return -1;
    if (is_zero_key(pubkey)) return -1;
    if (ov->n >= NORN_OVERLAY_MAX_PEERS) return -1;
    if (find_peer(ov, pubkey)) return -1;
    norn_overlay_peer_t *p = &ov->peers[ov->n++];
    memcpy(p->pubkey, pubkey, NORN_PUBKEY_BYTES);
    p->ip = ip;
    p->port = port;
    return 0;
}

int norn_overlay_peer_count(const norn_overlay_t *ov) { return ov ? ov->n : -1; }

int norn_overlay_find(const norn_overlay_t *ov,
                      const unsigned char pubkey[NORN_PUBKEY_BYTES],
                      norn_overlay_peer_t *out) {
    if (!ov || !pubkey) return -1;
    const norn_overlay_peer_t *p = find_peer(ov, pubkey);
    if (!p) return -1;
    if (out) *out = *p;
    return 0;
}

int norn_overlay_to_config(const norn_overlay_t *ov, const char *version,
                           norn_config_t *cfg,
                           uint32_t *ip_buf, uint16_t *port_buf, size_t cap) {
    if (!ov || !cfg || !ip_buf || !port_buf) return -1;
    if (ov->n == 0 || cap < (size_t)ov->n) return -1;
    for (int i = 0; i < ov->n; i++) {
        ip_buf[i] = ov->peers[i].ip;
        port_buf[i] = ov->peers[i].port;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->version = version;
    cfg->private_mode = 1; /* never announce on the public mainline DHT */
    cfg->boot_ips = ip_buf;
    cfg->boot_ports = port_buf;
    cfg->boot_count = ov->n;
    return ov->n;
}

int norn_overlay_pubkeys(const norn_overlay_t *ov,
                         unsigned char out[][NORN_PUBKEY_BYTES], size_t cap) {
    if (!ov || !out) return -1;
    if (cap < (size_t)ov->n) return -1;
    for (int i = 0; i < ov->n; i++)
        memcpy(out[i], ov->peers[i].pubkey, NORN_PUBKEY_BYTES);
    return ov->n;
}
