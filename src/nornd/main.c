/* SPDX-License-Identifier: MIT */
/**
 * @file main.c
 * @brief nornd — node + cluster host + Unix-socket IPC server (FEAT-029).
 *
 * This is daemon glue (socket and event-loop wiring), not unit-coverage code;
 * the testable parts live in ipc.c (codec), identity.c (SSH key), and
 * dispatch.c (request→response mapping). main() loads the SSH identity, brings
 * up a libnorn client and a norn_cluster, then runs one poll() loop that drives
 * the node + cluster timers and services IPC clients via nornd_dispatch.
 *
 * Peer transport: a single-node cluster (the default) needs no transport and is
 * fully functional. Multi-node frame transport over norn sessions is the
 * remaining integration seam — cluster_send() is where it plugs in.
 */

#include "config.h"
#include "agent.h"
#include "crypto.h"
#include "dispatch.h"
#include "identity.h"
#include "ipc.h"
#include "localstore.h"
#include "norn.h"
#include "norn_cluster.h"
#include "norn_raft.h"
#include "peers.h"
#include "served.h"
#include "served_conn.h"
#include "store.h"
#include "transport.h"

#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 64

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ---- ssh-agent signer (FEAT-028) ----
 *
 * When the identity key lives in ssh-agent, the node's public key still comes
 * from the `<identity>.pub` line, but signing is delegated to the agent over
 * $SSH_AUTH_SOCK so the secret never enters this process. The signer is
 * installed via norn_set_signer; a fresh agent connection is made per signature
 * (handshakes are infrequent), keeping the agent protocol strictly
 * request/response. */
typedef struct {
    unsigned char pub[32];
} agent_signer_t;

static int connect_unix(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(sa.sun_path)) {
        close(fd);
        return -1;
    }
    strcpy(sa.sun_path, path);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int agent_io_write(void *c, const unsigned char *b, size_t n) {
    int fd = *(int *)c;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, b + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}
static int agent_io_read(void *c, unsigned char *b, size_t n) {
    int fd = *(int *)c;
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, b + off, n - off);
        if (r == 0) return -1; /* EOF before the full reply */
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)r;
    }
    return 0;
}

static int agent_sign_cb(void *ud, unsigned char sig[64], const unsigned char *msg,
                         size_t msglen) {
    agent_signer_t *a = ud;
    const char *sock = getenv("SSH_AUTH_SOCK");
    if (!sock || !sock[0]) return -1;
    int fd = connect_unix(sock);
    if (fd < 0) return -1;
    nornd_agent_io_t io = {&fd, agent_io_write, agent_io_read};
    int rc = nornd_agent_sign_io(&io, a->pub, msg, msglen, sig);
    close(fd);
    return rc;
}

/* ---- cluster <-> backend glue ---- */

static int be_put(void *c, const unsigned char *k, size_t kl,
                  const unsigned char *v, size_t vl) {
    return norn_cluster_kv_put((norn_cluster_t *)c, k, kl, v, vl);
}
static int be_del(void *c, const unsigned char *k, size_t kl) {
    return norn_cluster_kv_del((norn_cluster_t *)c, k, kl);
}
static int be_get(void *c, const unsigned char *k, size_t kl, unsigned char *o,
                  size_t cap) {
    return norn_cluster_kv_get((norn_cluster_t *)c, k, kl, o, cap);
}
static int be_is_leader(void *c) {
    return norn_cluster_is_leader((norn_cluster_t *)c);
}
static const unsigned char *be_leader(void *c) {
    return norn_cluster_leader((norn_cluster_t *)c);
}
static int be_members(void *c, unsigned char out[][NORND_PUBKEY], int max) {
    return norn_cluster_members((norn_cluster_t *)c, out, max);
}
static int be_scan(void *c, const unsigned char *prefix, size_t plen,
                   norn_kv_visit_fn fn, void *ud) {
    return norn_cluster_kv_list((norn_cluster_t *)c, prefix, plen, fn, ud);
}

/* ---- node-served KV host (FEAT-033) ----
 *
 * A peer dials this node and opens a NORN_SVC_SERVED_KV stream; we serve GET/LIST
 * from the replicated cluster KV and CAT from the file-backed object store. Each
 * inbound served stream gets a serve-conn, driven each poll iteration. */
#define MAX_SERVE 32
typedef struct {
    nornd_served_backend_t be;
    nornd_serve_conn_t conns[MAX_SERVE];
    int n;
    localstore_t lstore;   /* `norn node set` keys: local, non-replicated */
} serve_host_t;

/* served-KV backend: read GET/LIST from the local store (node-set keys). */
static int served_get(void *ctx, const unsigned char *k, size_t kl,
                      unsigned char *out, size_t cap) {
    serve_host_t *h = ctx;
    return localstore_get(&h->lstore, k, kl, out, cap);
}
static int served_list(void *ctx, const unsigned char *prefix, size_t plen,
                       norn_kv_visit_fn fn, void *ud) {
    serve_host_t *h = ctx;
    return localstore_list(&h->lstore, prefix, plen, fn, ud);
}

static void on_served_accept(norn_stream_t *stream, void *ud) {
    serve_host_t *h = ud;
    if (h->n >= MAX_SERVE) return; /* table full — drop */
    nornd_serve_conn_init(&h->conns[h->n++], stream);
}

static void serve_host_pump(serve_host_t *h) {
    for (int i = 0; i < h->n;) {
        if (nornd_serve_conn_pump(&h->conns[i], &h->be))
            h->conns[i] = h->conns[--h->n]; /* finished — reclaim the slot */
        else
            i++;
    }
}

/* ---- watch subscriptions (FEAT-030 event stream) ----
 *
 * A `watch <prefix>` request turns its client socket into a subscriber. One
 * cluster watch (empty prefix = all keys) is registered for the daemon's
 * lifetime; on each applied change it fans out an encoded event frame to every
 * subscriber whose prefix matches the key. Best-effort writes — dead sockets
 * are reaped by the poll loop, which also drops their subscriptions. */
typedef struct {
    int fd;
    unsigned char prefix[NORND_IPC_MAX_KEY];
    size_t plen;
} watch_sub_t;

typedef struct {
    watch_sub_t subs[MAX_CLIENTS];
    int n;
} watch_set_t;

/* Drop every subscription for `fd` (idempotent; called on disconnect and
 * before re-subscribing the same socket). */
static void watch_drop(watch_set_t *ws, int fd) {
    for (int i = 0; i < ws->n; i++) {
        if (ws->subs[i].fd == fd) {
            ws->subs[i] = ws->subs[--ws->n];
            i--;
        }
    }
}

static void watch_add(watch_set_t *ws, int fd, const unsigned char *prefix,
                      size_t plen) {
    watch_drop(ws, fd);
    if (ws->n >= MAX_CLIENTS) return;
    watch_sub_t *s = &ws->subs[ws->n++];
    s->fd = fd;
    if (plen > sizeof(s->prefix)) plen = sizeof(s->prefix);
    memcpy(s->prefix, prefix, plen);
    s->plen = plen;
}

static void on_kv_change(void *ud, norn_kv_event_t ev, const unsigned char *key,
                         size_t klen, const unsigned char *val, size_t vlen) {
    watch_set_t *ws = ud;
    nornd_ipc_resp_t resp;
    nornd_watch_event(&resp, ev, key, klen, val, vlen);
    unsigned char frame[NORND_IPC_MAX_BODY + 4];
    int flen = nornd_ipc_encode_resp(&resp, frame, sizeof(frame));
    if (flen < 0) return;
    for (int i = 0; i < ws->n; i++) {
        if (ws->subs[i].plen <= klen &&
            memcmp(ws->subs[i].prefix, key, ws->subs[i].plen) == 0)
            (void)send(ws->subs[i].fd, frame, (size_t)flen, MSG_NOSIGNAL);
    }
}

/* ---- IPC socket ---- */

static const char *default_identity(char *buf, size_t cap) {
    const char *config = getenv("XDG_CONFIG_HOME");
    if (!config || !config[0]) {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/root";
        }
        /* System users (e.g. the norn daemon) often have no real home directory.
         * Fall back to a path under the StateDirectory (/var/lib/nornd) which is
         * writable even with ProtectSystem=strict. */
        if (strcmp(home, "/nonexistent") == 0 || strcmp(home, "/") == 0)
            snprintf(buf, cap, "/var/lib/nornd/identity");
        else
            snprintf(buf, cap, "%s/.config/norn/identity", home);
    } else {
        snprintf(buf, cap, "%s/norn/identity", config);
    }
    return buf;
}

/* Auto-generate a raw Ed25519 keypair at path when no --identity is given and
 * the default path does not yet exist (first run). */
static int auto_generate_identity(const char *path, char *err, size_t errcap) {
    /* Create parent directory. */
    char dir[512];
    size_t n = strlen(path);
    if (n >= sizeof(dir)) {
        snprintf(err, errcap, "identity path too long");
        return -1;
    }
    memcpy(dir, path, n + 1);
    char *slash = strrchr(dir, '/');
    if (!slash) {
        snprintf(err, errcap, "identity path has no parent directory");
        return -1;
    }
    *slash = '\0';
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        snprintf(err, errcap, "mkdir %s: %s", dir, strerror(errno));
        return -1;
    }
    keypair_t kp;
    if (crypto_keypair_new(&kp) != 0) {
        snprintf(err, errcap, "crypto_keypair_new failed");
        return -1;
    }
    if (crypto_keypair_save(&kp, path) != 0) {
        snprintf(err, errcap, "failed to write %s", path);
        return -1;
    }
    return 0;
}

static const char *default_socket(char *buf, size_t cap) {
    const char *run = getenv("XDG_RUNTIME_DIR");
    if (run && run[0]) {
        snprintf(buf, cap, "%s/nornd.sock", run);
        return buf;
    }
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.config/norn", home);
    mkdir(dir, 0700);
    snprintf(buf, cap, "%s/nornd.sock", dir);
    return buf;
}

/* systemd/launchd socket activation: if a listening fd was passed in, use it
 * instead of binding our own. Returns the fd (SD_LISTEN_FDS_START) or -1. */
static int activated_fd(void) {
    const char *nfds = getenv("LISTEN_FDS");
    const char *lpid = getenv("LISTEN_PID");
    if (!nfds || !lpid) return -1;
    if (atol(lpid) != (long)getpid()) return -1;
    if (atol(nfds) < 1) return -1;
    return 3; /* SD_LISTEN_FDS_START */
}

/* Minimal sd_notify(READY=1) for Type=notify, without linking libsystemd. */
static void notify_ready(void) {
    const char *path = getenv("NOTIFY_SOCKET");
    if (!path || !path[0]) return;
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (strlen(path) < sizeof(sa.sun_path)) {
        strcpy(sa.sun_path, path);
        if (sa.sun_path[0] == '@') sa.sun_path[0] = '\0'; /* abstract socket */
        static const char msg[] = "READY=1";
        sendto(fd, msg, sizeof(msg) - 1, 0, (struct sockaddr *)&sa, sizeof(sa));
    }
    close(fd);
}

static int listen_unix(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(sa.sun_path)) {
        close(fd);
        return -1;
    }
    strcpy(sa.sun_path, path);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
        listen(fd, 16) != 0) {
        close(fd);
        return -1;
    }
    chmod(path, 0600);
    return fd;
}

/* Context threaded through to serve_client so node-stats & peers IPC handlers
 * can access the norn_client (DHT routing table), backend (cluster state), and
 * process metadata (PID, uptime). */
typedef struct {
    const nornd_backend_t *be;
    watch_set_t *ws;
    const keypair_t *kp;
    norn_client_t *client;
    time_t start_time;
    uint16_t listen_port;   /* the DHT UDP port we listen on (for the self row) */
    localstore_t *lstore;    /* `norn node set` keys (the served local store) */
} serve_ctx_t;

/* Read, decode, dispatch and reply to one ready client. Returns 0 to keep the
 * connection, -1 to drop it. */
static int serve_client(int fd, serve_ctx_t *ctx) {
    unsigned char buf[NORND_IPC_MAX_BODY + 4];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) return -1;
    nornd_ipc_req_t req;
    size_t consumed = 0;
    if (nornd_ipc_decode_req(buf, (size_t)n, &req, &consumed) != 0) return -1;
    nornd_ipc_resp_t resp;
    memset(&resp, 0, sizeof(resp));

    /* Node identity queries and health check — handled before the cluster
     * dispatch so they work regardless of cluster state. */
    if (strcmp(req.op, "ping") == 0) {
        resp.ok = 1;
        memcpy(resp.val, "pong", 4);
        resp.vlen = 4;
        resp.has_val = 1;
    } else if (strcmp(req.op, "node-set") == 0) {
        /* `norn node set <name> <value>`: store a local (non-replicated) key in
         * the served store. req.key = name, req.val = value. */
        if (req.klen == 0 || !req.has_val || !ctx->lstore) {
            resp.ok = 0;
            resp.has_err = 1;
            strcpy(resp.err, "missing key or value");
        } else if (localstore_put(ctx->lstore, req.key, req.klen,
                                  req.val, req.vlen) != 0) {
            resp.ok = 0;
            resp.has_err = 1;
            strcpy(resp.err, "store full or key too large");
        } else {
            resp.ok = 1;
        }
    } else if (strcmp(req.op, "node-public") == 0) {
        resp.ok = 1;
        resp.has_val = 1;
        memcpy(resp.val, ctx->kp->public_key, 32);
        resp.vlen = 32;
    } else if (strcmp(req.op, "peer-public") == 0) {
        /* `norn peer public <node-id>`: resolve a peer's Ed25519 pubkey by
         * 40-hex DHT node id, from the local routing table (norn "pk"). */
        if (req.klen != 20) {
            resp.ok = 0;
            resp.has_err = 1;
            strcpy(resp.err, "node-id must be 20 bytes");
        } else {
            unsigned char pk[32];
            int found = norn_routing_pubkey(ctx->client, req.key, pk);
            if (found == 1) {
                resp.ok = 1;
                resp.has_val = 1;
                memcpy(resp.val, pk, 32);
                resp.vlen = 32;
            } else {
                resp.ok = 0;
                resp.has_err = 1;
                strcpy(resp.err, found == 0 ? "pubkey unknown (not a norn peer)" : "lookup error");
            }
        }
    } else if (strcmp(req.op, "node-secret") == 0) {
        resp.ok = 1;
        resp.has_val = 1;
        memcpy(resp.val, ctx->kp->secret_key, 64);
        resp.vlen = 64;
    } else if (strcmp(req.op, "node-stats") == 0) {
        resp.ok = 1;
        resp.has_val = 1;
        pid_t pid = getpid();
        time_t uptime = time(NULL) - ctx->start_time;
        int dht_nodes = norn_routing_size(ctx->client);
        int is_leader = ctx->be->is_leader(ctx->be->ctx) ? 1 : 0;
        int n_members = 0;
        {
            unsigned char m[NORND_IPC_MAX_ITEMS][NORND_PUBKEY];
            n_members = ctx->be->members(ctx->be->ctx, m, NORND_IPC_MAX_ITEMS);
            if (n_members < 0) n_members = 0;
        }
        int wn = snprintf((char *)resp.val, sizeof(resp.val),
            "pid=%ld\nuptime=%ld\ndht_nodes=%d\nis_leader=%d\ncluster_members=%d\n",
            (long)pid, (long)uptime, dht_nodes, is_leader, n_members);
        if (wn > 0) { resp.vlen = (size_t)wn; }
    } else if (strcmp(req.op, "peers") == 0) {
        resp.ok = 1;
        resp.has_val = 1;
        int wn = snprintf((char *)resp.val, sizeof(resp.val),
            "dht_nodes=%d\n", norn_routing_size(ctx->client));
        if (wn > 0) { resp.vlen = (size_t)wn; }
    } else if (strcmp(req.op, "peer-list") == 0) {
        /* Render the DHT routing table as TSV: a header line, then one row
         * per node (Node-Id, IP, Port, Age, Norn-Version, Application,
         * App-Version). The local nornd is emitted as the first data row so
         * the node sees itself. Application/App-Version come from the BEP-5
         * "v" vendor field (split on the first space: name, then version);
         * "mainline" is the fallback when a peer advertises nothing. Output
         * is capped to resp.val capacity; nodes that don't fit are omitted. */
        resp.ok = 1;
        resp.has_val = 1;
        norn_routing_node_t nodes[64];
        int n = norn_routing_nodes(ctx->client, nodes, 64);
        if (n < 0) n = 0;
        time_t now = time(NULL);
        size_t off = 0;
        int wn = snprintf((char *)resp.val + off, sizeof(resp.val) - off,
            "Node-Id\tIP\tPort\tAge\tNorn-Version\tApplication\tApp-Version\n");
        if (wn > 0) off += (size_t)wn;

        /* Helper to append one row, splitting the BEP-5 v string into
         * application name + version. Split on the first space (the BEP-5
         * convention, e.g. "Transmission 4.0.6"); if there is no space, split
         * on the first '/' (the norn user-agent convention, e.g.
         * "norn-node/0.12"). */
        #define APPEND_ROW(idhex_v, ip_v, port_v, age_v, pv_v, app_v) \
            do { \
                const char *_app = (app_v)[0] ? (app_v) : "mainline"; \
                const char *_sp = strchr(_app, ' '); \
                const char *_sl = strchr(_app, '/'); \
                const char *_sep = _sp ? _sp : _sl; \
                char _name[24] = {0}, _ver[24] = {0}; \
                if (_sep) { \
                    size_t _nl = (size_t)(_sep - _app); \
                    if (_nl >= sizeof(_name)) _nl = sizeof(_name) - 1; \
                    memcpy(_name, _app, _nl); _name[_nl] = '\0'; \
                    size_t _vl = strlen(_sep + 1); \
                    if (_vl >= sizeof(_ver)) _vl = sizeof(_ver) - 1; \
                    memcpy(_ver, _sep + 1, _vl); _ver[_vl] = '\0'; \
                } else { \
                    size_t _nl = strlen(_app); \
                    if (_nl >= sizeof(_name)) _nl = sizeof(_name) - 1; \
                    memcpy(_name, _app, _nl); _name[_nl] = '\0'; \
                } \
                wn = snprintf((char *)resp.val + off, sizeof(resp.val) - off, \
                    "%s\t%s\t%u\t%ld\t%s\t%s\t%s\n", \
                    idhex_v, ip_v, (unsigned)(port_v), (long)(age_v), \
                    (pv_v)[0] ? (pv_v) : "-", \
                    _name[0] ? _name : "mainline", \
                    _ver[0] ? _ver : "-"); \
                if (wn < 0) break; \
                if ((size_t)wn >= sizeof(resp.val) - off) break; \
                off += (size_t)wn; \
            } while (0)

        /* Self row: the local nornd. The application version is the full
         * norn version (NORN_VERSION), since the user-agent is "norn-node/<v>". */
        unsigned char self_id[20];
        if (norn_get_id(ctx->client, self_id) == 0) {
            char idhex[41];
            for (int b = 0; b < 20; b++) snprintf(idhex + b * 2, 3, "%02x", self_id[b]);
            uint32_t extip = 0; uint16_t extport = 0; int have_ext = 0;
            norn_external_addr(ctx->client, &extip, &extport, &have_ext);
            char ipbuf[INET_ADDRSTRLEN] = "0.0.0.0";
            if (have_ext) {
                struct in_addr a; a.s_addr = extip;
                inet_ntop(AF_INET, &a, ipbuf, sizeof(ipbuf));
            }
            char self_pv[8] = {0};
            /* self_pv = major.minor of NORN_VERSION (e.g. "0.12" from "0.12.1"). */
            {
                const char *pv = NORN_VERSION;
                const char *d1 = strchr(pv, '.');
                const char *end = d1 ? strchr(d1 + 1, '.') : NULL;
                size_t l = end ? (size_t)(end - pv) : strlen(pv);
                if (l >= sizeof(self_pv)) l = sizeof(self_pv) - 1;
                memcpy(self_pv, pv, l); self_pv[l] = '\0';
            }
            /* norn-node/<NORN_VERSION> so the macro's '/'-split yields the
             * full version as App-Version. */
            char self_app[40];
            snprintf(self_app, sizeof(self_app), "norn-node/%s", NORN_VERSION);
            APPEND_ROW(idhex, ipbuf, ctx->listen_port, 0, self_pv, self_app);
        }

        for (int i = 0; i < n && off + 1 < sizeof(resp.val); i++) {
            char ipbuf[INET_ADDRSTRLEN];
            struct in_addr a; a.s_addr = nodes[i].ip;
            inet_ntop(AF_INET, &a, ipbuf, sizeof(ipbuf));
            long age = (long)(now - nodes[i].last_seen);
            if (age < 0) age = 0;
            char idhex[41];
            for (int b = 0; b < 20; b++)
                snprintf(idhex + b * 2, 3, "%02x", nodes[i].id[b]);
            APPEND_ROW(idhex, ipbuf, ntohs(nodes[i].port), age, nodes[i].pv, nodes[i].app);
        }
        #undef APPEND_ROW
        resp.vlen = off;
    } else if (strcmp(req.op, "bep44-list") == 0) {
        /* `norn bep44 list`: enumerate the DHT records this node is holding on
         * behalf of the network, as TSV (Target, Type, Size, Seq, Stored). */
        resp.ok = 1;
        resp.has_val = 1;
        size_t off = 0;
        int wn = snprintf((char *)resp.val + off, sizeof(resp.val) - off,
            "Target\tType\tSize\tSeq\tStored\n");
        if (wn > 0) off += (size_t)wn;
        /* mutable then immutable */
        for (int kind = 0; kind < 2; kind++) {
            norn_dht_item_t items[64];
            int n = norn_dht_list(kind, items, 64);
            if (n < 0) n = 0;
            for (int i = 0; i < n && off + 1 < sizeof(resp.val); i++) {
                char tgt[41];
                for (int b = 0; b < 20; b++)
                    snprintf(tgt + b * 2, 3, "%02x", items[i].target[b]);
                wn = snprintf((char *)resp.val + off, sizeof(resp.val) - off,
                    "%s\t%s\t%zu\t%u\t%ld\n", tgt,
                    items[i].immutable ? "immutable" : "mutable",
                    items[i].vlen, items[i].seq, items[i].stored);
                if (wn < 0) break;
                if ((size_t)wn >= sizeof(resp.val) - off) break;
                off += (size_t)wn;
            }
        }
        resp.vlen = off;
    } else {
        nornd_dispatch(ctx->be, &req, &resp);
    }
    unsigned char out[NORND_IPC_MAX_BODY + 4];
    int olen = nornd_ipc_encode_resp(&resp, out, sizeof(out));
    if (olen < 0) return -1;
    if (send(fd, out, (size_t)olen, MSG_NOSIGNAL) != olen) return -1;
    /* A successful `watch <prefix>` turns this socket into an event stream. */
    if (resp.ok && strcmp(req.op, "watch") == 0)
        watch_add(ctx->ws, fd, req.key, req.klen);
    return 0;
}

static norn_node_class_t parse_class(const char *s) {
    if (strcmp(s, "server") == 0) return NORN_NODE_SERVER;
    if (strcmp(s, "workstation") == 0) return NORN_NODE_WORKSTATION;
    if (strcmp(s, "laptop") == 0) return NORN_NODE_LAPTOP;
    return NORN_NODE_MOBILE;
}

int main(int argc, char **argv) {
    char idbuf[600], sockbuf[600];
    const char *idpath = NULL, *sockpath = NULL;
    const char *data_dir = NULL;
    norn_node_class_t cls = NORN_NODE_SERVER;
    nornd_peer_t peers[RAFT_MAX_NODES];
    int n_peers = 0;
    uint16_t listen_port = 6881;   /* default: standard Mainline DHT port */
    int use_agent = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--identity") == 0 && i + 1 < argc)
            idpath = argv[++i];
        else if (strcmp(argv[i], "--agent") == 0)
            use_agent = 1; /* sign handshakes via ssh-agent (FEAT-028) */
        else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc)
            sockpath = argv[++i];
        else if (strcmp(argv[i], "--class") == 0 && i + 1 < argc)
            cls = parse_class(argv[++i]);
        else if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc)
            data_dir = argv[++i]; /* object store for node-served CAT (FEAT-033) */
        else if (strcmp(argv[i], "--foreground") == 0)
            ; /* we never daemonize; accepted for Type=notify units */
        else if (strcmp(argv[i], "--listen-port") == 0 && i + 1 < argc)
            listen_port = (uint16_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--peer") == 0 && i + 1 < argc) {
            if (n_peers >= RAFT_MAX_NODES ||
                nornd_peer_parse(argv[++i], &peers[n_peers]) != 0) {
                fprintf(stderr, "nornd: bad --peer spec (want <64-hex-pubkey>"
                                "[@host:port])\n");
                return 2;
            }
            n_peers++;
        } else {
            fprintf(stderr,
                    "usage: nornd [--identity PATH] [--agent] [--socket PATH] "
                    "[--class server|workstation|laptop|mobile]\n"
                    "             [--data-dir PATH] [--foreground] "
                    "[--listen-port PORT]\n"
                    "             [--peer <64-hex-pubkey>[@host:port]] ...\n"
                    "  --agent  sign handshakes via ssh-agent (public key from "
                    "<identity>.pub)\n");
            return 2;
        }
    }
    int idpath_explicit = idpath != NULL;
    if (!idpath) idpath = default_identity(idbuf, sizeof(idbuf));
    if (!sockpath) sockpath = default_socket(sockbuf, sizeof(sockbuf));

    /* Auto-generate an ed25519 identity when the default path is used and the
     * file does not already exist (first run). Explicit --identity failures are
     * still surfaced as errors. */
    if (!idpath_explicit && access(idpath, F_OK) != 0) {
        char gen_err[128];
        if (auto_generate_identity(idpath, gen_err, sizeof(gen_err)) != 0) {
        fprintf(stderr, "nornd: no identity at %s and auto-generation "
                        "failed: %s\n", idpath, gen_err);
            fprintf(stderr, "nornd: specify --identity or ensure ~/.config/norn"
                            " is writable\n");
            return 1;
        }
    }

    keypair_t kp;
    agent_signer_t agentsig;
    memset(&agentsig, 0, sizeof(agentsig));
    if (use_agent) {
        /* Identity public key from `<idpath>.pub`; the secret stays in the agent.
         * `kp.secret_key` is a placeholder — agent_sign_cb does all signing. */
        char pubpath[640];
        char line[512];
        size_t ln = 0;
        snprintf(pubpath, sizeof(pubpath), "%s.pub", idpath);
        FILE *pf = fopen(pubpath, "rb");
        if (pf) {
            ln = fread(line, 1, sizeof(line) - 1, pf);
            fclose(pf);
        }
        if (!pf ||
            nornd_identity_parse_pubkey_line(line, ln, agentsig.pub) != 0) {
            fprintf(stderr,
                    "nornd: --agent needs %s with an ssh-ed25519 public key\n",
                    pubpath);
            return 1;
        }
        if (!getenv("SSH_AUTH_SOCK")) {
            fprintf(stderr, "nornd: --agent set but SSH_AUTH_SOCK is unset\n");
            return 1;
        }
        memcpy(kp.public_key, agentsig.pub, 32);
        memset(kp.secret_key, 0, sizeof(kp.secret_key));
    } else if (crypto_keypair_load(&kp, idpath) != 0) {
        fprintf(stderr, "nornd: failed to load identity from %s\n", idpath);
        return 1;
    }

    norn_config_t ncfg;
    memset(&ncfg, 0, sizeof(ncfg));
    ncfg.version = "nornd/0.12";
    norn_client_t *client = norn_new(kp.public_key, kp.secret_key, &ncfg);
    if (!client) {
        fprintf(stderr, "nornd: failed to create norn client\n");
        return 1;
    }
    /* Route handshake signing through ssh-agent when so configured. */
    if (use_agent) norn_set_signer(client, agent_sign_cb, &agentsig);

    /* Multi-node frame transport over norn sessions (single-node: no peers). */
    nornd_transport_t *xport =
        nornd_transport_new(client, listen_port, peers, n_peers);
    if (!xport) {
        fprintf(stderr, "nornd: failed to create transport\n");
        norn_free(client);
        return 1;
    }

    norn_cluster_io_t io = {nornd_transport_send, xport};
    norn_cluster_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.self_class = cls;
    ccfg.election_eligible = -1; /* derive eligibility from the node class */
    norn_cluster_t *cl = norn_cluster_new(kp.public_key, &io, &ccfg);
    if (!cl) {
        fprintf(stderr, "nornd: failed to create cluster\n");
        nornd_transport_free(xport);
        norn_free(client);
        return 1;
    }
    /* Configured peers are voting servers; wire inbound frames to the cluster. */
    for (int i = 0; i < n_peers; i++)
        norn_cluster_add_member(cl, peers[i].pubkey, NORN_NODE_SERVER, 1);
    nornd_transport_set_cluster(xport, cl);

    int activated = 0;
    int lfd = activated_fd();
    if (lfd >= 0) {
        activated = 1; /* fd handed over by systemd/launchd; don't unlink it */
    } else {
        lfd = listen_unix(sockpath);
    }
    if (lfd < 0) {
        fprintf(stderr, "nornd: cannot listen on %s: %s\n", sockpath,
                strerror(errno));
        norn_cluster_free(cl);
        norn_free(client);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    nornd_backend_t be = {cl,         be_put,    be_del,     be_get,
                          be_is_leader, be_leader, be_members, be_scan};

    /* One cluster watch for the daemon's lifetime; on_kv_change fans applied
     * changes out to `watch` subscribers (empty prefix = every key). */
    watch_set_t watches;
    memset(&watches, 0, sizeof(watches));
    norn_cluster_kv_watch(cl, (const unsigned char *)"", 0, on_kv_change,
                          &watches);

    serve_ctx_t srv = {&be, &watches, &kp, client, time(NULL), listen_port, NULL};

    /* Node-served KV (FEAT-033): host GET/LIST from the cluster KV and CAT from a
     * file-backed object store under <data-dir>/objects, served to peers that
     * dial NORN_SVC_SERVED_KV. Best-effort: if the store dir can't be created we
     * simply don't advertise served content. */
    serve_host_t serve;
    memset(&serve, 0, sizeof(serve));
    static nornd_store_t serve_store;
    char dht_nodes_path[640];
    {
        char objdir[640];
        const char *base = data_dir;
        char homebuf[600];
        if (!base) {
            const char *home = getenv("HOME");
            if (!home) home = "/tmp";
            snprintf(homebuf, sizeof(homebuf), "%s/.config/norn", home);
            base = homebuf;
        }
        mkdir(base, 0700); /* ensure the parent exists before the object dir */
        /* The local served-KV store is always available for `norn node set`,
         * independent of the optional file-backed object store. */
        localstore_init(&serve.lstore);
        srv.lstore = &serve.lstore;
        snprintf(objdir, sizeof(objdir), "%s/objects", base);
        if (nornd_store_init(&serve_store, objdir) == 0) {
            serve.be.ctx = &serve;
            serve.be.get = served_get;
            serve.be.list = served_list;
            serve.be.store = &serve_store;
            norn_register_stream_service(client, NORN_SVC_SERVED_KV,
                                         on_served_accept, &serve);
        }
        /* DHT routing-table persistence path. */
        snprintf(dht_nodes_path, sizeof(dht_nodes_path), "%s/dht_nodes", base);
    }
    /* Load previously saved DHT routing-table nodes so we re-join the
     * network faster than a full re-bootstrap. Best-effort: if the file
     * doesn't exist (first start) or is corrupt, we simply start fresh. */
    {
        int loaded = norn_load_dht_nodes(client, dht_nodes_path);
        if (loaded > 0)
            fprintf(stderr, "nornd: loaded %d cached DHT nodes\n", loaded);
    }

    fprintf(stderr, "nornd: serving %s (identity %s)\n",
            activated ? "socket-activated fd" : sockpath, idpath);
    notify_ready();

    int clients[MAX_CLIENTS];
    int nclients = 0;
    int nfd = norn_get_fd(client);
    time_t last_dht_save = 0;         /* last time we persisted the routing table */
    time_t last_announce = 0;          /* last time we announced under our node-id */
    unsigned char self_nodeid[20];
    int have_self_nodeid = (norn_get_id(client, self_nodeid) == 0);

    while (!g_stop) {
        struct pollfd pfds[2 + MAX_CLIENTS];
        int np = 0;
        if (nfd >= 0) {
            pfds[np].fd = nfd;
            pfds[np].events = POLLIN;
            np++;
        }
        int lidx = np;
        pfds[np].fd = lfd;
        pfds[np].events = POLLIN;
        np++;
        int cstart = np;
        for (int i = 0; i < nclients; i++) {
            pfds[np].fd = clients[i];
            pfds[np].events = POLLIN;
            np++;
        }

        int rc = poll(pfds, np, 10);
        norn_tick(client);
        nornd_transport_poll(xport); /* drain peer frames into the cluster */
        serve_host_pump(&serve);     /* drive node-served KV streams (FEAT-033) */
        norn_cluster_tick(cl, now_ms());

        /* Periodically persist the DHT routing table so we re-join faster
         * after a restart. Every 300s (matching MAINLINE_BOOTSTRAP_INTERVAL). */
        {
            time_t now = time(NULL);
            if (now - last_dht_save >= 300) {
                int saved = norn_save_dht_nodes(client, dht_nodes_path);
                if (saved > 0) last_dht_save = now;
            }
            /* Announce ourselves under our node-id info_hash so peers can
             * resolve us via get_peers(node-id) and dial for served-KV. The
             * norn pk extension is published with the announce. */
            if (have_self_nodeid && now - last_announce >= 300 &&
                norn_routing_size(client) > 0) {
                norn_announce(client, self_nodeid);
                last_announce = now;
            }
        }

        if (rc <= 0) continue;

        if (nfd >= 0 && (pfds[0].revents & POLLIN)) norn_tick(client);

        if (pfds[lidx].revents & POLLIN) {
            int afd = accept(lfd, NULL, NULL);
            if (afd >= 0) {
                if (nclients < MAX_CLIENTS)
                    clients[nclients++] = afd;
                else
                    close(afd);
            }
        }

        for (int i = 0; i < nclients; i++) {
            if (pfds[cstart + i].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (serve_client(clients[i], &srv) != 0) {
                    watch_drop(&watches, clients[i]);
                    close(clients[i]);
                    clients[i] = clients[--nclients];
                    i--;
                }
            }
        }
    }

    fprintf(stderr, "nornd: shutting down\n");
    for (int i = 0; i < nclients; i++) close(clients[i]);
    close(lfd);
    if (!activated) unlink(sockpath); /* systemd owns an activated socket */
    norn_cluster_free(cl);
    nornd_transport_free(xport);
    /* Save routing table one final time so the most recent node set is
     * available on next start. */
    norn_save_dht_nodes(client, dht_nodes_path);
    norn_free(client);
    return 0;
}
