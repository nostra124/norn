#ifndef STREAMMUX_H
#define STREAMMUX_H

#include <stddef.h>
#include <stdint.h>
#include "stream.h"

/* FEAT-074: logical-stream multiplexing over ONE secure channel. Today each
 * shell/copy/tunnel runs its own channel + handshake; the mux lets many
 * independent, ordered byte-streams share a single established channel — one
 * handshake serves concurrent shell + copy + tunnels, and it is the substrate the
 * real-time media path (FEAT-076) needs to separate media from control.
 *
 * Each logical stream is a full stream_t (so FEAT-072 loss recovery + FEAT-073
 * congestion/flow control apply per stream). The mux frames every segment with a
 * 2-byte stream-id prefix, then hands it to the caller's send callback (which
 * seals + sends it over the channel); inbound segments are demultiplexed by that
 * id. Loss on one logical stream does not stall delivery on another (each has its
 * own reassembly). */

#define STREAMMUX_MAX 16        /* max concurrent logical streams per channel */
#define STREAMMUX_FRAME 2       /* stream-id prefix bytes */

typedef struct streammux streammux_t;

/* Emit one framed segment (id-prefixed). The caller seals it (channel_seal) and
 * sends the datagram. Return 0 on success, non-zero to retry on the next tick. */
typedef int (*streammux_send_fn)(void *ctx, const unsigned char *seg, size_t len);

streammux_t *streammux_new(streammux_send_fn send, void *ctx);
void streammux_free(streammux_t *m);

/* Open a logical stream by id (idempotent). Returns 0 on success, -1 if full. */
int streammux_open(streammux_t *m, uint16_t sid);

/* Queue app bytes on a logical stream (opens it if needed). Returns bytes
 * accepted, or -1 on error/full. */
int streammux_write(streammux_t *m, uint16_t sid, const unsigned char *data, size_t len, uint32_t now_ms);

/* Read delivered, in-order bytes from a logical stream. Returns bytes copied. */
int streammux_read(streammux_t *m, uint16_t sid, unsigned char *out, size_t cap);

/* Feed one inbound framed segment; routed to its logical stream (auto-opened if
 * the peer initiated it). */
void streammux_input(streammux_t *m, const unsigned char *seg, size_t len, uint32_t now_ms);

/* Drive all logical streams' timers. */
void streammux_tick(streammux_t *m, uint32_t now_ms);

/* Bytes available to read on a logical stream (0 if it doesn't exist). */
size_t streammux_readable(const streammux_t *m, uint16_t sid);

/* Number of open logical streams. */
int streammux_count(const streammux_t *m);

/* Half-close / peer-FIN / send-drained, per logical stream (delegate to its
 * stream_t; missing streams report as finished/drained). */
void streammux_finish(streammux_t *m, uint16_t sid, uint32_t now_ms);
int streammux_peer_finished(const streammux_t *m, uint16_t sid);
int streammux_send_done(const streammux_t *m, uint16_t sid);

#endif
