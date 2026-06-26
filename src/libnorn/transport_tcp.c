/* TCP stream transport implementation — wraps a connected TCP socket
 * in the transport vtable. Production wiring for stream I/O (shell/copy). */
#include "transport.h"
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

typedef struct {
    int fd;
    int should_close;
} tcp_state_t;

static norn_mode_t tcp_modes(const norn_transport_t *t) {
    (void)t;
    return NORN_STREAM;
}

static uint32_t tcp_cap(const norn_transport_t *t) {
    (void)t;
    return 0;  /* no special caps for raw TCP */
}

static int tcp_dial(norn_transport_t *t, const void *ep, size_t eplen) {
    (void)t; (void)ep; (void)eplen;
    return 0;  /* TCP: dial is implicit (socket already connected) */
}

static int tcp_send(norn_transport_t *t, const void *buf, size_t len) {
    tcp_state_t *s = t->state;
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(s->fd, (const unsigned char *)buf + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;  /* LCOV_EXCL_LINE */
            return -1;  /* LCOV_EXCL_LINE */
        }
        off += (size_t)n;
    }
    return (int)len;
}

static int tcp_recv(norn_transport_t *t, void *buf, size_t cap) {
    tcp_state_t *s = t->state;
    ssize_t n = recv(s->fd, buf, cap, 0);
    if (n < 0) {
        if (errno == EINTR) return 0;  /* LCOV_EXCL_LINE */
        return -1;  /* LCOV_EXCL_LINE */
    }
    return (int)n;
}

static int tcp_local_endpoint(norn_transport_t *t, void *out, size_t cap) {
    tcp_state_t *s = t->state;
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    if (getsockname(s->fd, (struct sockaddr *)&ss, &sl) != 0) return -1;
    /* Return sockaddr for TCP */
    if (cap < sl) return -1;
    memcpy(out, &ss, sl);
    return (int)sl;
}

static void tcp_close(norn_transport_t *t) {
    if (!t) return;   /* LCOV_EXCL_BR_LINE: dispatcher guards NULL before calling close */
    tcp_state_t *s = t->state;
    if (s) {   /* LCOV_EXCL_BR_LINE: state is always allocated for a live transport */
        if (s->should_close && s->fd >= 0) close(s->fd);   /* LCOV_EXCL_BR_LINE: fd is always >= 0 (constructor rejects fd < 0) */
        free(s);
    }
    free(t);
}

static const norn_transport_ops_t TCP_OPS = {
    tcp_modes, tcp_cap, tcp_dial, tcp_send, tcp_recv, tcp_local_endpoint, tcp_close,
};

norn_transport_t *norn_tcp_new(int fd, int should_close) {
    if (fd < 0) return NULL;
    norn_transport_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;   /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    tcp_state_t *s = calloc(1, sizeof(*s));
    if (!s) { free(t); return NULL; }   /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    s->fd = fd;
    s->should_close = should_close;
    t->ops = &TCP_OPS;
    t->state = s;
    return t;
}