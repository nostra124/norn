#ifndef NORN_TRANSPORT_H
#define NORN_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>

/* Transport abstraction for datagram and stream I/O. A transport INSTANCE
 * carries an ops vtable + opaque state; the core uses NULL-safe dispatchers,
 * so a new carrier (QUIC, etc.) is a new ops table.
 *
 * Delivery mode is FIRST-CLASS: DATAGRAM (unreliable) and STREAM (reliable).
 * A transport may offer one or both; BOTH satisfies either over one connection. */

typedef enum {
    NORN_DATAGRAM = 1,   /* unreliable, message boundaries preserved (UDP, QUIC DATAGRAM) */
    NORN_STREAM   = 2,   /* reliable, ordered byte stream (TCP, QUIC streams) */
    NORN_BOTH     = 3,   /* offers both over one connection (QUIC) */
} norn_mode_t;

typedef struct norn_transport norn_transport_t;

typedef struct {
    norn_mode_t (*modes)(const norn_transport_t *t);                       /* declared delivery mode(s) */
    uint32_t    (*cap)(const norn_transport_t *t);                         /* capability flags (0 = none) */
    int   (*dial)(norn_transport_t *t, const void *ep, size_t eplen);       /* open/connect; 0 ok, -1 err */
    int   (*send)(norn_transport_t *t, const void *buf, size_t len);         /* bytes sent, -1 err */
    int   (*recv)(norn_transport_t *t, void *buf, size_t cap);               /* bytes (0 = none), -1 err/closed */
    int   (*local_endpoint)(norn_transport_t *t, void *out, size_t cap);     /* bytes written, -1 */
    void  (*close)(norn_transport_t *t);
} norn_transport_ops_t;

struct norn_transport {
    const norn_transport_ops_t *ops;
    void *state;
};

/* Dispatchers — NULL-safe: NULL transport, NULL ops, or missing op returns error. */
norn_mode_t norn_transport_modes(const norn_transport_t *t);
uint32_t    norn_transport_cap(const norn_transport_t *t);
int         norn_transport_is_datagram(const norn_transport_t *t);   /* mode has DATAGRAM */
int         norn_transport_is_stream(const norn_transport_t *t);     /* mode has STREAM */
int   norn_transport_dial(norn_transport_t *t, const void *ep, size_t eplen);
int   norn_transport_send(norn_transport_t *t, const void *buf, size_t len);
int   norn_transport_recv(norn_transport_t *t, void *buf, size_t cap);
int   norn_transport_local_endpoint(norn_transport_t *t, void *out, size_t cap);
void  norn_transport_close(norn_transport_t *t);

#endif /* NORN_TRANSPORT_H */
