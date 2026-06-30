/* SPDX-License-Identifier: MIT */
/**
 * @file peers.h
 * @brief Multi-node cluster transport helpers for nornd (FEAT-029 multi-node).
 *
 * Pure pieces of the peer transport: parsing a `--peer` spec and de-framing the
 * byte stream a peer sends. A peer is addressed by its Ed25519 public key. By
 * default its endpoint is resolved over the Mainline DHT (the glue in main.c
 * calls norn_dial_async); a spec may instead carry an explicit `@host:port`
 * for a direct dial (norn_dial_direct_async) on a LAN or without the DHT. The
 * cluster hands `cluster_send` opaque wire frames; nornd length-prefixes each
 * (4-byte big-endian) onto a norn stream, and on the receive side reassembles
 * them with the framer here before calling norn_cluster_input.
 */
#ifndef NORND_PEERS_H
#define NORND_PEERS_H

#include <stddef.h>
#include <stdint.h>

#define NORND_PEER_PUBKEY   32
#define NORND_PEER_HOST_MAX 256
#define NORND_FRAME_MAX     8192 /* a cluster wire frame is small (Raft entry) */
#define NORND_FRAMER_CAP    (NORND_FRAME_MAX + 4)

/** A parsed `--peer` spec: a pubkey, optionally with a direct endpoint. */
typedef struct {
    unsigned char pubkey[NORND_PEER_PUBKEY];
    int direct;                     /* 1 if an explicit host:port was given */
    char host[NORND_PEER_HOST_MAX]; /* valid when direct (DHT-resolved if not) */
    uint16_t port;                  /* valid when direct */
} nornd_peer_t;

/**
 * Parse a peer spec into `out`:
 *   `<64-hex-pubkey>`              -> DHT resolution (direct = 0)
 *   `<64-hex-pubkey>@<host>:<port>`-> direct dial    (direct = 1)
 * Splits the endpoint on the last ':' so IPv6 literals work.
 * @return 0 on success, -1 on a malformed key / host / port.
 */
int nornd_peer_parse(const char *spec, nornd_peer_t *out);

/**
 * Prepend a 4-byte big-endian length to `payload`, writing the framed bytes to
 * `out`. Returns the total framed size, or -1 (payload too big / won't fit).
 */
int nornd_frame_encode(const unsigned char *payload, size_t len,
                       unsigned char *out, size_t cap);

/** Incremental reassembler for length-prefixed frames off a byte stream. */
typedef struct {
    unsigned char buf[NORND_FRAMER_CAP];
    size_t start; /* offset of the first unread byte */
    size_t end;   /* offset past the last buffered byte */
} nornd_framer_t;

/** Reset a framer to empty. */
void nornd_framer_reset(nornd_framer_t *f);

/**
 * Append received stream bytes. Returns 0 on success, -1 if the data would
 * overflow the framer (a malformed/oversized stream); the framer is left reset.
 */
int nornd_framer_push(nornd_framer_t *f, const unsigned char *data, size_t len);

/**
 * Pop the next complete frame. Returns 1 and points `*payload`/`*plen` at the
 * frame body inside the framer (valid until the next push) on success; 0 if no
 * complete frame is buffered yet; -1 if the next frame's declared length
 * exceeds NORND_FRAME_MAX (a protocol violation — drop the connection).
 */
int nornd_framer_next(nornd_framer_t *f, const unsigned char **payload,
                      size_t *plen);

#endif /* NORND_PEERS_H */
