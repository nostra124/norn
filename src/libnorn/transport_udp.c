/* UDP transport implementation — wraps a socket fd in the transport vtable.
 * Production wiring for datagram I/O (channel handshake, etc.). */
#include "transport.h"
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

typedef struct {
    int fd;
    int should_close;
} udp_state_t;

static norn_mode_t udp_modes(const norn_transport_t *t) {
    (void)t;
    return NORN_DATAGRAM;
}

static uint32_t udp_cap(const norn_transport_t *t) {
    (void)t;
    return 0;  /* no special caps for raw UDP */
}

static int udp_dial(norn_transport_t *t, const void *ep, size_t eplen) {
    (void)t; (void)ep; (void)eplen;
    return 0;  /* UDP: dial is implicit (sendto on each packet) */
}

static int udp_send(norn_transport_t *t, const void *buf, size_t len) {
    udp_state_t *s = t->state;
    /* For a connected UDP socket, send() works; for unconnected, caller must
     * use sendto directly. This send is for the connected case. */
    ssize_t n = send(s->fd, buf, len, 0);
    if (n < 0) return -1;
    return (int)n;
}

static int udp_recv(norn_transport_t *t, void *buf, size_t cap) {
    udp_state_t *s = t->state;
    ssize_t n = recv(s->fd, buf, cap, 0);
    if (n < 0) return -1;
    return (int)n;
}

static int udp_local_endpoint(norn_transport_t *t, void *out, size_t cap) {
    udp_state_t *s = t->state;
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    if (getsockname(s->fd, (struct sockaddr *)&ss, &sl) != 0) return -1;
    /* Return the sockaddr bytes (caller knows the family) */
    if (cap < sl) return -1;
    memcpy(out, &ss, sl);
    return (int)sl;
}

static void udp_close(norn_transport_t *t) {
    if (!t) return;   /* LCOV_EXCL_BR_LINE: dispatcher guards NULL before calling close */
    udp_state_t *s = t->state;
    if (s) {   /* LCOV_EXCL_BR_LINE: state is always allocated for a live transport */
        if (s->should_close && s->fd >= 0) close(s->fd);   /* LCOV_EXCL_BR_LINE: fd is always >= 0 (constructor rejects fd < 0) */
        free(s);
    }
    free(t);
}

static const norn_transport_ops_t UDP_OPS = {
    udp_modes, udp_cap, udp_dial, udp_send, udp_recv, udp_local_endpoint, udp_close,
};

norn_transport_t *norn_udp_new(int fd, int should_close) {
    if (fd < 0) return NULL;
    udp_state_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;   /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    s->fd = fd;
    s->should_close = should_close;
    norn_transport_t *t = calloc(1, sizeof(*t));
    if (!t) { free(s); return NULL; }   /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    t->ops = &UDP_OPS;
    t->state = s;
    return t;
}