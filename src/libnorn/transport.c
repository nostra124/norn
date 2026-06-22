#include "transport.h"
#include <stdlib.h>
#include <string.h>

norn_mode_t norn_transport_modes(const norn_transport_t *t) {
    return (t && t->ops && t->ops->modes) ? t->ops->modes(t) : (norn_mode_t)0;
}

uint32_t norn_transport_cap(const norn_transport_t *t) {
    return (t && t->ops && t->ops->cap) ? t->ops->cap(t) : 0;
}

int norn_transport_is_datagram(const norn_transport_t *t) { return (norn_transport_modes(t) & NORN_DATAGRAM) != 0; }
int norn_transport_is_stream(const norn_transport_t *t)   { return (norn_transport_modes(t) & NORN_STREAM) != 0; }

int norn_transport_dial(norn_transport_t *t, const void *ep, size_t eplen) {
    return (t && t->ops && t->ops->dial) ? t->ops->dial(t, ep, eplen) : -1;
}

int norn_transport_send(norn_transport_t *t, const void *buf, size_t len) {
    return (t && t->ops && t->ops->send) ? t->ops->send(t, buf, len) : -1;
}

int norn_transport_recv(norn_transport_t *t, void *buf, size_t cap) {
    return (t && t->ops && t->ops->recv) ? t->ops->recv(t, buf, cap) : -1;
}

int norn_transport_local_endpoint(norn_transport_t *t, void *out, size_t cap) {
    return (t && t->ops && t->ops->local_endpoint) ? t->ops->local_endpoint(t, out, cap) : -1;
}

void norn_transport_close(norn_transport_t *t) {
    if (t && t->ops && t->ops->close) t->ops->close(t);
}