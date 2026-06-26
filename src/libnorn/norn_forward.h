/**
 * @file norn_forward.h
 * @brief Generic stream-tunnel engine — service-over-pubkey (FEAT-018).
 *
 * A norn stream is TCP-equivalent (reliable, ordered, encrypted, addressed by
 * public key). This module provides the reusable core that splices an existing
 * byte transport (a local TCP/Unix socket) to a norn stream, so any HTTP / line
 * / JSON protocol rides norn exactly as it rides TCP — the ssh `-L`/`-R`
 * equivalent.
 *
 * The engine — `norn_pump_t` — is a bidirectional byte pump between two opaque
 * endpoints (`norn_forward_io_t`). It is deliberately transport-agnostic: each
 * side is a small callback vtable, so the pump is fully unit-testable without
 * sockets or a network. The `norn-forward` CLI supplies fd-backed endpoints for
 * one side and norn-stream-backed endpoints for the other; an embedding app can
 * supply whatever it likes.
 *
 * @par Threading / Loop Model
 * Single-threaded and non-blocking, like the rest of norn: the host drives the
 * pump from its event loop via norn_pump_drive(), interleaved with norn_tick().
 *
 * @par Memory
 * Bounded: each direction has a fixed buffer (bufsize). Backpressure is implicit
 * — when the destination cannot accept more, the source is not read, so memory
 * never exceeds 2 * bufsize per pump.
 */

#ifndef NORN_FORWARD_H
#define NORN_FORWARD_H

#include <stddef.h>

/**
 * @brief One endpoint of a pump. All callbacks are non-blocking.
 *
 * `read` and `write` are required; `shutdown` and `close` are optional (NULL is
 * fine). The `ctx` pointer passed to norn_pump_new() is handed back to every
 * callback unchanged.
 */
typedef struct {
    /**
     * Read up to `cap` bytes into `buf`.
     * @return >0 bytes read; 0 nothing available now (would block);
     *         -1 end of stream (peer closed / EOF); -2 fatal error.
     */
    int (*read)(void *ctx, unsigned char *buf, size_t cap);
    /**
     * Write up to `len` bytes from `buf`. May accept fewer (backpressure).
     * @return >=0 bytes accepted; -2 fatal error.
     */
    int (*write)(void *ctx, const unsigned char *buf, size_t len);
    /** Half-close this endpoint's write side (peer saw EOF upstream). Optional. */
    void (*shutdown)(void *ctx);
    /** Release endpoint resources. Optional. Called by norn_pump_free(). */
    void (*close)(void *ctx);
} norn_forward_io_t;

/** @brief Opaque pump handle. */
typedef struct norn_pump norn_pump_t;

/** @brief Pump status. */
typedef enum {
    NORN_PUMP_ACTIVE = 0,   /**< Still forwarding in at least one direction. */
    NORN_PUMP_DONE,         /**< Both directions reached EOF and drained. */
    NORN_PUMP_ERROR         /**< A fatal endpoint error occurred. */
} norn_pump_status_t;

/** @brief Default per-direction buffer size (bytes) when bufsize is 0. */
#define NORN_PUMP_DEFAULT_BUF 16384

/** @brief Largest per-direction buffer accepted (bounds memory). */
#define NORN_PUMP_MAX_BUF     (1u << 20)

/**
 * @brief Create a pump splicing endpoint A <-> endpoint B.
 *
 * @param a   Endpoint A vtable (copied; must have read+write).
 * @param a_ctx Borrowed context for A's callbacks (must outlive the pump).
 * @param b   Endpoint B vtable (copied; must have read+write).
 * @param b_ctx Borrowed context for B's callbacks (must outlive the pump).
 * @param bufsize Per-direction buffer in bytes; 0 selects NORN_PUMP_DEFAULT_BUF.
 *               Clamped to NORN_PUMP_MAX_BUF.
 * @return New pump, or NULL on invalid args / allocation failure.
 *
 * @note NULL-safe: returns NULL if a, b, or any required callback is NULL.
 */
norn_pump_t *norn_pump_new(const norn_forward_io_t *a, void *a_ctx,
                           const norn_forward_io_t *b, void *b_ctx,
                           size_t bufsize);

/**
 * @brief Run one non-blocking iteration.
 *
 * Moves as many bytes as possible A->B and B->A, propagating EOF and half-close.
 * Safe to call repeatedly; once DONE or ERROR it is a no-op returning that state.
 *
 * @param p Pump handle.
 * @return Current status after the iteration (NORN_PUMP_ERROR if p is NULL).
 */
norn_pump_status_t norn_pump_drive(norn_pump_t *p);

/**
 * @brief Current status without driving.
 * @return Status, or NORN_PUMP_ERROR if p is NULL.
 */
norn_pump_status_t norn_pump_status(const norn_pump_t *p);

/**
 * @brief Bytes forwarded so far in each direction (observability).
 *
 * @param p Pump handle.
 * @param a_to_b Out: bytes written into B (from A). May be NULL.
 * @param b_to_a Out: bytes written into A (from B). May be NULL.
 *
 * @note NULL-safe: does nothing if p is NULL.
 */
void norn_pump_stats(const norn_pump_t *p, size_t *a_to_b, size_t *b_to_a);

/**
 * @brief Free the pump, closing both endpoints (if they provided `close`).
 * @param p Pump handle (may be NULL).
 */
void norn_pump_free(norn_pump_t *p);

#endif /* NORN_FORWARD_H */
