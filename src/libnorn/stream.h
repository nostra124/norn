/* SPDX-License-Identifier: MIT */
#ifndef STREAM_H
#define STREAM_H

#include <stddef.h>
#include <stdint.h>

/* A minimal reliable, ordered byte stream over an unreliable datagram channel
 * (FEAT-003 slice 3a). The shell/copy/tunnel features ride this. Cumulative ACK
 * with a selective-ack (SACK) bitmap, adaptive RTT-estimated RTO (Jacobson/Karn)
 * and fast retransmit on duplicate ACKs (FEAT-072) — so a single loss retransmits
 * only the missing segment, not the whole window. No external dependencies.
 *
 * Transport-agnostic: the stream hands finished segments to a send callback
 * (the caller seals them with channel_seal and sends the datagram) and is fed
 * inbound segment plaintext (post channel_open) via stream_input. Timers are
 * driven by stream_tick with a caller-supplied monotonic millisecond clock, so
 * it slots into any event loop and is testable without sockets. */

#define STREAM_SEG_PAYLOAD 1024     /* max app bytes per segment */
#define STREAM_HEADER 11            /* flags(1) + seq(4) + ack(4) + rwnd(2, FEAT-073 flow control) */
#define STREAM_SEG_MAX (STREAM_HEADER + STREAM_SEG_PAYLOAD)

typedef struct stream stream_t;

/* Emit one ready segment. Return 0 on success, non-zero to signal the segment
 * could not be sent (it will be retried on the next tick). */
typedef int (*stream_send_fn)(void *ctx, const unsigned char *seg, size_t len);

stream_t *stream_new(stream_send_fn send, void *ctx);
void stream_free(stream_t *s);

/* Queue application bytes for reliable, ordered delivery. Returns the number of
 * bytes accepted (< len when the send buffer is full; retry the remainder). */
int stream_write(stream_t *s, const unsigned char *data, size_t len, uint32_t now_ms);

/* Feed one inbound segment (the plaintext channel_open produced). */
void stream_input(stream_t *s, const unsigned char *seg, size_t len, uint32_t now_ms);

/* Copy delivered, in-order application bytes out. Returns bytes copied. */
int stream_read(stream_t *s, unsigned char *out, size_t cap);

/* Drive retransmit / delayed-ACK timers. Call regularly. */
void stream_tick(stream_t *s, uint32_t now_ms);

/* Begin an orderly half-close: a FIN follows the buffered data. */
void stream_finish(stream_t *s, uint32_t now_ms);

/* Bytes currently available to stream_read. */
size_t stream_readable(const stream_t *s);

/* The peer sent FIN and all its data has been delivered. */
int stream_peer_finished(const stream_t *s);

/* Our FIN has been acknowledged (everything we sent is delivered). */
int stream_send_done(const stream_t *s);

/* Current retransmit timeout (ms) — adaptive (RTT-estimated, FEAT-072). Exposed
 * for observability/tests and for future RTT-aware pacing. */
uint32_t stream_rto_ms(const stream_t *s);

/* Congestion window + in-flight, in segments (FEAT-073 AIMD). Observability/tests. */
uint32_t stream_cwnd(const stream_t *s);
uint32_t stream_inflight(const stream_t *s);

#endif
