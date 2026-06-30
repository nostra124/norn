/* SPDX-License-Identifier: MIT */
/* TCP stream transport — wraps a connected TCP socket in the transport vtable. */
#ifndef NORN_TRANSPORT_TCP_H
#define NORN_TRANSPORT_TCP_H
#include "transport.h"

/* Wrap a connected TCP socket in the transport vtable.
 * The socket should already be connected (either from accept() or connect()).
 * Returns a heap-allocated norn_transport_t on success, NULL on failure.
 * Caller owns the returned pointer and must call norn_transport_close() to free it.
 * If should_close is non-zero, norn_transport_close will close the fd. */
norn_transport_t *norn_tcp_new(int fd, int should_close);

#endif /* NORN_TRANSPORT_TCP_H */