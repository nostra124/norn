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

#include "dispatch.h"
#include "identity.h"
#include "ipc.h"
#include "keydir.h"
#include "norn.h"
#include "norn_cluster.h"
#include "norn_raft.h"
#include "peers.h"
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

/* ---- IPC socket ---- */

static const char *default_identity(char *buf, size_t cap) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/root";
    }
    snprintf(buf, cap, "%s/.ssh/id_ed25519", home);
    return buf;
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
    snprintf(dir, sizeof(dir), "%s/.norn", home);
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

/* Read, decode, dispatch and reply to one ready client. Returns 0 to keep the
 * connection, -1 to drop it. */
static int serve_client(int fd, const nornd_backend_t *be) {
    unsigned char buf[NORND_IPC_MAX_BODY + 4];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) return -1;
    nornd_ipc_req_t req;
    size_t consumed = 0;
    if (nornd_ipc_decode_req(buf, (size_t)n, &req, &consumed) != 0) return -1;
    nornd_ipc_resp_t resp;
    nornd_dispatch(be, &req, &resp);
    unsigned char out[NORND_IPC_MAX_BODY + 4];
    int olen = nornd_ipc_encode_resp(&resp, out, sizeof(out));
    if (olen < 0) return -1;
    if (send(fd, out, (size_t)olen, MSG_NOSIGNAL) != olen) return -1;
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
    norn_node_class_t cls = NORN_NODE_SERVER;
    nornd_peer_t peers[RAFT_MAX_NODES];
    int n_peers = 0;
    uint16_t listen_port = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--identity") == 0 && i + 1 < argc)
            idpath = argv[++i];
        else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc)
            sockpath = argv[++i];
        else if (strcmp(argv[i], "--class") == 0 && i + 1 < argc)
            cls = parse_class(argv[++i]);
        else if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc)
            ++i; /* accepted for the service units; state dir is reserved */
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
                    "usage: nornd [--identity PATH] [--socket PATH] "
                    "[--class server|workstation|laptop|mobile]\n"
                    "             [--data-dir PATH] [--foreground] "
                    "[--listen-port PORT]\n"
                    "             [--peer <64-hex-pubkey>[@host:port]] ...\n");
            return 2;
        }
    }
    if (!idpath) idpath = default_identity(idbuf, sizeof(idbuf));
    if (!sockpath) sockpath = default_socket(sockbuf, sizeof(sockbuf));

    keypair_t kp;
    char err[128];
    if (nornd_identity_load_file(idpath, &kp, err, sizeof(err)) != 0) {
        fprintf(stderr, "nornd: identity %s: %s\n", idpath, err);
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

    nornd_backend_t be = {cl,         be_put,    be_del,    be_get,
                          be_is_leader, be_leader, be_members};

    /* Read our SSH public-key line (`<identity>.pub`) to publish into the fleet
     * key directory once we can lead. Optional: skip if it isn't there. */
    char sshline[512];
    size_t sshlen = 0;
    {
        char pubpath[640];
        snprintf(pubpath, sizeof(pubpath), "%s.pub", idpath);
        FILE *pf = fopen(pubpath, "rb");
        if (pf) {
            sshlen = fread(sshline, 1, sizeof(sshline) - 1, pf);
            fclose(pf);
            while (sshlen > 0 &&
                   (sshline[sshlen - 1] == '\n' || sshline[sshlen - 1] == '\r'))
                sshlen--;
            sshline[sshlen] = '\0';
        }
    }
    int ssh_published = 0;

    fprintf(stderr, "nornd: serving %s (identity %s)\n",
            activated ? "socket-activated fd" : sockpath, idpath);
    notify_ready();

    int clients[MAX_CLIENTS];
    int nclients = 0;
    int nfd = norn_get_fd(client);

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
        norn_cluster_tick(cl, now_ms());

        /* Once we can lead, publish our SSH public key to the directory. */
        if (!ssh_published && sshlen > 0 && norn_cluster_is_leader(cl)) {
            if (nornd_keydir_put_ssh(&be, kp.public_key, sshline) == 0)
                ssh_published = 1;
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
                if (serve_client(clients[i], &be) != 0) {
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
    norn_free(client);
    return 0;
}
