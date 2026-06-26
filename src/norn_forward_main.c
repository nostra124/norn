/*
 * norn-forward — service-over-pubkey stream tunnel (FEAT-018).
 *
 * Client mode (`-L`): listen on a local TCP port and splice every accepted
 * connection to a remote peer addressed only by its public key, over an
 * encrypted norn stream. This is the `ssh -L` equivalent: any HTTP/line/JSON
 * client reaches the remote service with no code changes and no TLS (norn
 * already encrypts end-to-end).
 *
 *     norn-forward -L 8080 <peer-pubkey-hex>
 *     curl http://localhost:8080/   # tunnels to the peer's service
 *
 * The reusable splice engine lives in libnorn (norn_forward.h); this file is
 * the socket + session glue (excluded from unit-coverage, like the norn CLI).
 *
 * Server mode (accept inbound peer streams → connect a local service) is the
 * mirror image and lands once the session layer exposes inbound-stream accept
 * (FEAT-016/017 completion); the splice engine and fd IO below are shared by it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sodium.h>

#include "config.h"
#include "libnorn/norn.h"
#include "libnorn/norn_session.h"
#include "libnorn/crypto.h"
#include "libnorn/norn_forward.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MAX_CONN 64

static volatile sig_atomic_t g_running = 1;
static void on_signal(int sig) { (void)sig; g_running = 0; }

/* ---- fd-backed IO endpoint (local TCP socket) ---- */

typedef struct { int fd; } fdctx_t;

static int fdio_read(void *ctx, unsigned char *buf, size_t cap) {
    int fd = ((fdctx_t *)ctx)->fd;
    if (cap > INT_MAX) cap = INT_MAX;
    ssize_t n = recv(fd, buf, cap, 0);
    if (n > 0) return (int)n;
    if (n == 0) return -1; /* peer closed */
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -2;
}

static int fdio_write(void *ctx, const unsigned char *buf, size_t len) {
    int fd = ((fdctx_t *)ctx)->fd;
    if (len > INT_MAX) len = INT_MAX;
    ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
    if (n >= 0) return (int)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -2;
}

static void fdio_shutdown(void *ctx) { shutdown(((fdctx_t *)ctx)->fd, SHUT_WR); }
static void fdio_close(void *ctx) {
    fdctx_t *c = ctx;
    if (c->fd >= 0) { close(c->fd); c->fd = -1; }
    free(c);
}

static const norn_forward_io_t FD_IO = { fdio_read, fdio_write, fdio_shutdown, fdio_close };

/* ---- norn-stream-backed IO endpoint ---- */

typedef struct { norn_stream_t *s; } strmctx_t;

static int strmio_read(void *ctx, unsigned char *buf, size_t cap) {
    norn_stream_t *s = ((strmctx_t *)ctx)->s;
    if (cap > INT_MAX) cap = INT_MAX;
    int n = norn_stream_read(s, buf, cap);
    if (n > 0) return n;
    if (n < 0) return -2;
    return norn_stream_peer_closed(s) ? -1 : 0;
}

static int strmio_write(void *ctx, const unsigned char *buf, size_t len) {
    norn_stream_t *s = ((strmctx_t *)ctx)->s;
    if (len > INT_MAX) len = INT_MAX;
    int n = norn_stream_write(s, buf, len);
    return n >= 0 ? n : -2;
}

static void strmio_shutdown(void *ctx) { norn_stream_close(((strmctx_t *)ctx)->s); }
static void strmio_close(void *ctx) { free(ctx); }

static const norn_forward_io_t STRM_IO = { strmio_read, strmio_write, strmio_shutdown, strmio_close };

/* ---- per-connection state ---- */

typedef enum { CONN_DIALING, CONN_READY, CONN_DEAD } conn_state_t;

typedef struct {
    int fd;                  /* accepted local socket */
    conn_state_t state;
    norn_session_t *session;
    norn_pump_t *pump;
} conn_t;

static conn_t g_conns[MAX_CONN];
static unsigned char g_peer[NORN_PUBKEY_BYTES];

static conn_t *conn_alloc(int fd) {
    for (int i = 0; i < MAX_CONN; i++) {
        if (g_conns[i].fd < 0) {
            g_conns[i].fd = fd;
            g_conns[i].state = CONN_DIALING;
            g_conns[i].session = NULL;
            g_conns[i].pump = NULL;
            return &g_conns[i];
        }
    }
    return NULL;
}

static void conn_kill(conn_t *c) {
    if (c->pump) { norn_pump_free(c->pump); c->pump = NULL; }  /* frees both IO ctxs + fd */
    else if (c->fd >= 0) { close(c->fd); }
    if (c->session) { norn_session_close_async(c->session, NULL, NULL); c->session = NULL; }
    c->fd = -1;
    c->state = CONN_DEAD;
}

/* Build the bidirectional pump once both the local fd and the norn stream are
 * ready. */
static void conn_start_pump(conn_t *c, norn_stream_t *stream) {
    fdctx_t *fc = calloc(1, sizeof(*fc));
    strmctx_t *sc = calloc(1, sizeof(*sc));
    if (!fc || !sc) { free(fc); free(sc); conn_kill(c); return; }
    fc->fd = c->fd;
    sc->s = stream;
    c->pump = norn_pump_new(&FD_IO, fc, &STRM_IO, sc, 0);
    if (!c->pump) { free(fc); free(sc); conn_kill(c); return; }
    c->state = CONN_READY;
}

static void on_stream(norn_stream_t *stream, norn_stream_state_t st, void *ud) {
    conn_t *c = ud;
    if (st == NORN_STREAM_READY) conn_start_pump(c, stream);
    else conn_kill(c);
}

static void on_session(norn_session_t *session, norn_session_state_t st, void *ud) {
    conn_t *c = ud;
    if (st == NORN_SESSION_ESTABLISHED) {
        c->session = session;
        if (!norn_stream_open_async(session, on_stream, c)) conn_kill(c);
    } else if (st == NORN_SESSION_CLOSED) {
        conn_kill(c);
    }
}

static int set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    return fl < 0 ? -1 : fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* === Server side: expose a local TCP service over norn (the `ssh -R` half) === */

static int g_service_port; /* local TCP service to forward inbound streams to */

static int dial_service(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    set_nonblock(fd);
    return fd;
}

/* A peer opened a stream: connect to the wrapped local service and splice. */
static void on_inbound_stream(norn_stream_t *stream, void *ud) {
    (void)ud;
    int svc = dial_service(g_service_port);
    if (svc < 0) {
        norn_stream_reset(stream);
        return;
    }
    conn_t *c = conn_alloc(svc);
    if (!c) {
        close(svc);
        norn_stream_reset(stream);
        return;
    }
    conn_start_pump(c, stream);
}

/* A peer dialed us: route its inbound streams to the local service. */
static void on_accept_session(norn_session_t *session, void *ud) {
    (void)ud;
    norn_session_set_accept_stream(session, on_inbound_stream, NULL);
}

static int make_listener(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 16) != 0) {
        close(fd);
        return -1;
    }
    set_nonblock(fd);
    return fd;
}

static void usage(FILE *out, const char *prog) {
    fprintf(out, "Usage:\n");
    fprintf(out, "  %s -L <local-port> <peer-pubkey-hex>   (client: local TCP -> peer)\n", prog);
    fprintf(out, "  %s -R <service-port>                   (server: peer streams -> local service)\n\n", prog);
    fprintf(out, "Tunnel a TCP service over an encrypted norn stream, addressed by public\n");
    fprintf(out, "key (the `ssh -L` / `ssh -R` equivalent). No TLS — norn encrypts E2E.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -L <port>            Client: local TCP port to listen on (loopback)\n");
    fprintf(out, "  -R <port>            Server: local TCP service to expose to peers\n");
    fprintf(out, "  --dht-port <port>    Local DHT/UDP port (default: ephemeral)\n");
    fprintf(out, "  --help               Show this help\n");
}

int main(int argc, char **argv) {
    const char *prog = "norn-forward";
    int local_port = -1, service_port = -1, dht_port = 0;
    const char *peer_hex = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout, prog);
            return 0;
        } else if (strcmp(argv[i], "-L") == 0 && i + 1 < argc) {
            local_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-R") == 0 && i + 1 < argc) {
            service_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--dht-port") == 0 && i + 1 < argc) {
            dht_port = atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            peer_hex = argv[i];
        } else {
            fprintf(stderr, "%s: unknown or incomplete option: %s\n", prog, argv[i]);
            usage(stderr, prog);
            return 1;
        }
    }

    int server_mode = service_port > 0;
    if (server_mode) {
        if (service_port > 65535) {
            usage(stderr, prog);
            return 1;
        }
    } else if (local_port <= 0 || local_port > 65535 || !peer_hex) {
        usage(stderr, prog);
        return 1;
    }
    if (sodium_init() < 0) {
        fprintf(stderr, "%s: libsodium init failed\n", prog);
        return 1;
    }
    if (!server_mode &&
        (strlen(peer_hex) != NORN_PUBKEY_BYTES * 2 ||
         sodium_hex2bin(g_peer, sizeof(g_peer), peer_hex, strlen(peer_hex),
                        NULL, NULL, NULL) != 0)) {
        fprintf(stderr, "%s: invalid peer pubkey (need %d hex chars)\n",
                prog, NORN_PUBKEY_BYTES * 2);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < MAX_CONN; i++) g_conns[i].fd = -1;

    keypair_t kp;
    if (crypto_keypair_new(&kp) != 0) {
        fprintf(stderr, "%s: keypair generation failed\n", prog);
        return 1;
    }
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = NORN_VERSION;

    norn_client_t *client = norn_new(kp.public_key, kp.secret_key, &cfg);
    if (!client) {
        fprintf(stderr, "%s: failed to create norn client\n", prog);
        return 1;
    }
    norn_bootstrap(client);

    int listen_fd = -1;
    if (server_mode) {
        g_service_port = service_port;
        if (norn_listen_async(client, htons((uint16_t)dht_port), NULL,
                              on_accept_session, NULL) != 0) {
            fprintf(stderr, "%s: failed to listen for norn sessions\n", prog);
            norn_free(client);
            return 2;
        }
        unsigned char selfpk[NORN_PUBKEY_BYTES];
        char hex[NORN_PUBKEY_BYTES * 2 + 1];
        memcpy(selfpk, kp.public_key, NORN_PUBKEY_BYTES);
        sodium_bin2hex(hex, sizeof(hex), selfpk, NORN_PUBKEY_BYTES);
        fprintf(stderr, "%s: exposing 127.0.0.1:%d to peers; our pubkey: %s\n",
                prog, service_port, hex);
    } else {
        listen_fd = make_listener(local_port);
        if (listen_fd < 0) {
            fprintf(stderr, "%s: cannot listen on 127.0.0.1:%d: %s\n",
                    prog, local_port, strerror(errno));
            norn_free(client);
            return 2;
        }
        fprintf(stderr, "%s: forwarding 127.0.0.1:%d -> %s\n", prog, local_port, peer_hex);
    }

    while (g_running) {
        norn_tick(client);

        /* Client mode: accept local TCP connections and dial the peer. */
        if (!server_mode) {
            int cfd = accept(listen_fd, NULL, NULL);
            if (cfd >= 0) {
                set_nonblock(cfd);
                conn_t *c = conn_alloc(cfd);
                if (!c) {
                    close(cfd); /* table full */
                } else if (norn_dial_async(client, g_peer, NULL, on_session, c) != 0) {
                    conn_kill(c);
                }
            }
        }

        for (int i = 0; i < MAX_CONN; i++) {
            conn_t *c = &g_conns[i];
            if (c->state == CONN_READY && c->pump) {
                norn_pump_status_t s = norn_pump_drive(c->pump);
                if (s != NORN_PUMP_ACTIVE) conn_kill(c);
            }
        }

        usleep(1000);
    }

    for (int i = 0; i < MAX_CONN; i++) {
        if (g_conns[i].state != CONN_DEAD && g_conns[i].fd >= 0) conn_kill(&g_conns[i]);
    }
    if (listen_fd >= 0) close(listen_fd);
    norn_free(client);
    return 0;
}
