/* SPDX-License-Identifier: MIT */
/**
 * @file norn_overlay.h
 * @brief Private-overlay bootstrap configuration (FEAT-020).
 *
 * A first-class "form a private pubkey-mesh from my own bootstrap node(s)" API.
 * A closed fleet (e.g. one org's agents) lists its bootstrap peers — each a
 * public key plus an endpoint — and gets a validated private-mode
 * `norn_config_t`: connectivity is pubkey-addressed and bootstrap goes only to
 * the fleet's own nodes, never announcing on the public mainline DHT.
 *
 * This module is the pure, deterministic configuration layer (build + validate
 * the peer set, project it into a `norn_config_t`); the actual overlay
 * formation runs in the DHT client using the produced config. See
 * docs/private-overlay.md for the public-bootstrap vs private-overlay modes.
 */

#ifndef NORN_OVERLAY_H
#define NORN_OVERLAY_H

#include <stddef.h>
#include <stdint.h>
#include "norn.h"

/** Max bootstrap peers in a private overlay. */
#define NORN_OVERLAY_MAX_PEERS 32

/** A fleet bootstrap peer: pubkey-addressed, with a dialable endpoint. */
typedef struct {
    unsigned char pubkey[NORN_PUBKEY_BYTES]; /**< peer identity */
    uint32_t ip;                              /**< IPv4 (network byte order) */
    uint16_t port;                            /**< UDP port (network byte order) */
} norn_overlay_peer_t;

typedef struct norn_overlay norn_overlay_t;

/* === Lifecycle === */

/** Create an empty private-overlay descriptor. NULL on allocation failure. */
norn_overlay_t *norn_overlay_new(void);
void norn_overlay_free(norn_overlay_t *ov);

/* === Peer set === */

/**
 * Add a fleet bootstrap peer. Rejects a NULL/zero pubkey, a zero ip/port, a
 * duplicate pubkey, and a full table.
 * @return 0 on success, -1 on error.
 */
int norn_overlay_add_peer(norn_overlay_t *ov,
                          const unsigned char pubkey[NORN_PUBKEY_BYTES],
                          uint32_t ip, uint16_t port);

/** Number of bootstrap peers. */
int norn_overlay_peer_count(const norn_overlay_t *ov);

/** Look up a peer by pubkey; copies it into `out` if found. Returns 0/-1. */
int norn_overlay_find(const norn_overlay_t *ov,
                      const unsigned char pubkey[NORN_PUBKEY_BYTES],
                      norn_overlay_peer_t *out);

/* === Projection to a norn_config_t === */

/**
 * Project the overlay into a private-mode `norn_config_t`.
 *
 * Fills `cfg` (private_mode = 1, version copied) and points its boot_ips /
 * boot_ports at the caller-provided arrays, which are filled from the peer set.
 * The caller owns `ip_buf` / `port_buf` and must keep them (and `version`)
 * alive as long as `cfg` is used.
 *
 * @param ov        overlay descriptor (must have >= 1 peer)
 * @param version   app version string stored in cfg (may be NULL)
 * @param cfg       output config (zeroed then filled)
 * @param ip_buf    caller array for boot IPs (>= peer_count)
 * @param port_buf  caller array for boot ports (>= peer_count)
 * @param cap       capacity of ip_buf/port_buf in elements
 * @return number of peers written, or -1 on error (no peers, cap too small).
 */
int norn_overlay_to_config(const norn_overlay_t *ov, const char *version,
                           norn_config_t *cfg,
                           uint32_t *ip_buf, uint16_t *port_buf, size_t cap);

/**
 * Copy the peers' public keys into `out` (>= peer_count entries), e.g. to seed
 * a cluster's membership from the overlay. Returns the count, or -1 on error.
 */
int norn_overlay_pubkeys(const norn_overlay_t *ov,
                         unsigned char out[][NORN_PUBKEY_BYTES], size_t cap);

#endif /* NORN_OVERLAY_H */
