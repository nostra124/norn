/* SPDX-License-Identifier: MIT */
/* UDP transport — wraps a socket fd in the transport vtable. */
#ifndef NORN_TRANSPORT_UDP_H
#define NORN_TRANSPORT_UDP_H
#include "transport.h"

/* Create a datagram transport from a UDP socket fd. If should_close is non-zero,
 * norn_transport_close will close the fd; otherwise the caller retains ownership.
 * Returns NULL on invalid fd or allocation failure. */
norn_transport_t *norn_udp_new(int fd, int should_close);

#endif /* NORN_TRANSPORT_UDP_H */