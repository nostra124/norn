/* SPDX-License-Identifier: MIT */
/* norn — Mainline DHT client CLI */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <sodium.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "config.h"
#include "nornd/cli_cluster.h"
#include "nornd/ipc.h"

#define DEFAULT_PORT 6881
#define DEFAULT_TIMEOUT 5000
#define MAX_VALUE_SIZE 1000

static const char *prog_name = "norn";

static char *key_file = NULL;
static int port = DEFAULT_PORT;
static int timeout_ms = DEFAULT_TIMEOUT;

/* Forward declarations for IPC helpers used before definition. */
static int ipc_round_trip_kv_bin(const char *op,
                                 const unsigned char *key, size_t klen,
                                 const unsigned char *value, size_t vlen,
                                 unsigned char *resp_val, size_t *resp_vlen,
                                 size_t resp_cap);

/* stdout is a terminal (colorize+align); when piped, emit plain recfile/tsv. */
static int stdout_is_tty(void) {
    return isatty(STDOUT_FILENO);
}

/* ANSI color helpers. Empty strings when not a tty so pipe output is plain. */
static const char *col_cyan(void)    { return stdout_is_tty() ? "\033[36m" : ""; }
static const char *col_magenta(void) { return stdout_is_tty() ? "\033[35m" : ""; }
static const char *col_bold(void)    { return stdout_is_tty() ? "\033[1m"  : ""; }
static const char *col_reset(void)   { return stdout_is_tty() ? "\033[0m"  : ""; }

/* Pretty-print a recfile (key=value\n…) with aligned colored keys when tty;
 * plain passthrough when piped. */
static void print_recfile_pretty(const unsigned char *buf, size_t len) {
    if (!stdout_is_tty()) {
        fwrite(buf, 1, len, stdout);
        return;
    }
    /* First pass: compute max key width for alignment. */
    size_t max_key = 0, i = 0;
    while (i < len) {
        size_t eq = i;
        while (eq < len && buf[eq] != '=' && buf[eq] != '\n') eq++;
        if (eq < len && buf[eq] == '=' && eq - i > max_key) max_key = eq - i;
        while (i < len && buf[i] != '\n') i++;
        if (i < len) i++;
    }
    /* Second pass: print aligned, colorized key=value lines. */
    i = 0;
    while (i < len) {
        size_t eq = i;
        while (eq < len && buf[eq] != '=' && buf[eq] != '\n') eq++;
        if (eq < len && buf[eq] == '=') {
            size_t klen = eq - i;
            printf("%s", col_cyan());
            fwrite(buf + i, 1, klen, stdout);
            for (size_t p = klen; p < max_key; p++) fputc(' ', stdout);
            printf("%s=%s", col_reset(), col_bold());
            size_t vstart = eq + 1, vend = vstart;
            while (vend < len && buf[vend] != '\n') vend++;
            fwrite(buf + vstart, 1, vend - vstart, stdout);
            printf("%s\n", col_reset());
            i = vend;
            if (i < len) i++;
        } else {
            size_t eol = i;
            while (eol < len && buf[eol] != '\n') eol++;
            fwrite(buf + i, 1, eol - i, stdout);
            if (eol < len) { fputc('\n', stdout); eol++; }
            i = eol;
        }
    }
}

/* Convert a recfile response (key=value\n) into TSV (key\tvalue\n) and
 * pretty-print with aligned columns + color when tty; plain TSV when piped. */
static void print_tsv_pretty(const unsigned char *buf, size_t len) {
    if (!stdout_is_tty()) {
        fwrite(buf, 1, len, stdout);
        return;
    }
    /* Collect rows; each row is a set of tab-separated cells. Compute column
     * widths for alignment. Cap rows/cells to fixed small buffers (output is
     * capped by the IPC val size anyway). */
    char cells[128][16][96];
    int widths[16] = {0};
    int nrows = 0, maxcols = 0;
    size_t i = 0;
    while (i < len && nrows < 128) {
        int ncols = 0;
        size_t cstart = i;
        while (i < len && buf[i] != '\n') {
            if (buf[i] == '\t') {
                if (ncols < 16) {
                    size_t cl = i - cstart;
                    if (cl >= 95) cl = 95;
                    memcpy(cells[nrows][ncols], buf + cstart, cl);
                    cells[nrows][ncols][cl] = '\0';
                    if ((int)cl > widths[ncols]) widths[ncols] = (int)cl;
                }
                ncols++;
                cstart = i + 1;
            }
            i++;
        }
        /* last cell (up to newline) */
        if (ncols < 16) {
            size_t cl = i - cstart;
            if (cl >= 95) cl = 95;
            memcpy(cells[nrows][ncols], buf + cstart, cl);
            cells[nrows][ncols][cl] = '\0';
            if ((int)cl > widths[ncols]) widths[ncols] = (int)cl;
            ncols++;
        }
        if (ncols > maxcols) maxcols = ncols;
        nrows++;
        if (i < len) i++;
    }
    /* Print: header row colored cyan+bold, data rows colored bold. */
    for (int r = 0; r < nrows; r++) {
        printf("%s", r == 0 ? col_cyan() : col_bold());
        for (int c = 0; c < maxcols; c++) {
            if (c > 0) printf("%s\t%s", col_reset(), r == 0 ? col_cyan() : col_bold());
            printf("%-*s", widths[c], cells[r][c]);
        }
        printf("%s\n", col_reset());
    }
}

static void usage(FILE *out) {
    fprintf(out, "Usage: %s [OPTIONS] <command> [ARGS...]\n", prog_name);
    fputc('\n', out);

    fprintf(out, "Options:\n");
    fprintf(out, "  --key <path>       Ed25519 keypair (default: ~/.config/norn/key.pem)\n");
    fprintf(out, "  --port <port>      DHT UDP port (default: 6881)\n");
    fprintf(out, "  --timeout <ms>     Query timeout (default: 5000)\n");
    fprintf(out, "  --help             Show this help\n");
    fputc('\n', out);

    fprintf(out, "Commands:\n");
    static const char *cmds[][2] = {
        {"node",       "Manage the local nornd daemon"},
        {"peer",       "Interact with remote peers"},
        {"bep44",      "DHT records (mutable + immutable)"},
        {"cluster",    "Cluster KV store"},
        {"version",    "Print version"},
    };
    for (size_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        fprintf(out, "  %-12s %s\n", cmds[i][0], cmds[i][1]);
    }
    fputc('\n', out);
}

static int do_version(void) {
    printf("norn %s\n", NORN_VERSION);
    return 0;
}


/* IPC helpers: socket path, connect, round-trip one request. */
static const char *nornd_socket_path(char *buf, size_t cap) {
    const char *env = getenv("NORN_SOCK");
    if (env && env[0]) return env;
    /* Prefer the per-user daemon socket, but fall back to the system nornd
     * socket (/run/nornd/nornd.sock) when no user socket exists — so a
     * desktop install talks to the system daemon by default. */
    const char *run = getenv("XDG_RUNTIME_DIR");
    if (run && run[0]) {
        snprintf(buf, cap, "%s/nornd.sock", run);
        if (access(buf, F_OK) == 0) return buf;
    }
    const char *home = getenv("HOME");
    if (home && home[0]) {
        char ubuf[600];
        snprintf(ubuf, sizeof(ubuf), "%s/.config/norn/nornd.sock", home);
        if (access(ubuf, F_OK) == 0) {
            size_t n = strlen(ubuf);
            if (n >= cap) n = cap - 1;
            memcpy(buf, ubuf, n); buf[n] = '\0';
            return buf;
        }
    }
    /* System socket fallback. */
    snprintf(buf, cap, "%s", "/run/nornd/nornd.sock");
    return buf;
}

static int ipc_connect(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(sa.sun_path)) { close(fd); return -1; }
    strcpy(sa.sun_path, path);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) { close(fd); return -1; }
    return fd;
}

static int ipc_round_trip(const char *op, unsigned char *val, size_t *vlen,
                          size_t cap) {
    char pbuf[512];
    const char *path = nornd_socket_path(pbuf, sizeof(pbuf));
    int fd = ipc_connect(path);
    if (fd < 0) {
        fprintf(stderr, "norn: cannot reach nornd at %s. Is nornd running?\n", path);
        return -1;
    }
    nornd_ipc_req_t req;
    memset(&req, 0, sizeof(req));
    strcpy(req.op, op);
    unsigned char wire[256];
    int wlen = nornd_ipc_encode_req(&req, wire, sizeof(wire));
    if (wlen < 0 || write(fd, wire, (size_t)wlen) != wlen) {
        close(fd);
        return -1;
    }
    unsigned char frame[NORND_IPC_MAX_BODY + 4];
    size_t got = 0;
    while (got < 4) {
        ssize_t n = read(fd, frame + got, 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    int64_t body = nornd_ipc_frame_len(frame, 4);
    if (body < 0 || (size_t)body + 4 > sizeof(frame)) { close(fd); return -1; }
    while (got < (size_t)body + 4) {
        ssize_t n = read(fd, frame + got, (size_t)body + 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    close(fd);
    nornd_ipc_resp_t resp;
    size_t consumed = 0;
    if (nornd_ipc_decode_resp(frame, got, &resp, &consumed) != 0) return -1;
    if (!resp.ok) return -1;
    size_t n = resp.vlen;
    if (n > cap) n = cap;
    memcpy(val, resp.val, n);
    *vlen = n;
    return 0;
}

/* ipc_round_trip variant that sends a key + value (for `node-set`). Returns 0
 * on success (resp.ok); -1 on transport/decode failure or a not-ok response. */
static int ipc_round_trip_kv(const char *op, const char *key, const char *value) {
    char pbuf[512];
    const char *path = nornd_socket_path(pbuf, sizeof(pbuf));
    int fd = ipc_connect(path);
    if (fd < 0) {
        fprintf(stderr, "norn: cannot reach nornd at %s. Is nornd running?\n", path);
        return -1;
    }
    nornd_ipc_req_t req;
    memset(&req, 0, sizeof(req));
    strcpy(req.op, op);
    size_t kl = strlen(key);
    if (kl > sizeof(req.key)) kl = sizeof(req.key);
    memcpy(req.key, key, kl);
    req.klen = kl;
    if (value) {
        size_t vl = strlen(value);
        if (vl > sizeof(req.val)) vl = sizeof(req.val);
        memcpy(req.val, value, vl);
        req.vlen = vl;
        req.has_val = 1;
    }
    unsigned char wire[NORND_IPC_MAX_BODY + 4];
    int wlen = nornd_ipc_encode_req(&req, wire, sizeof(wire));
    if (wlen < 0 || write(fd, wire, (size_t)wlen) != wlen) {
        close(fd);
        return -1;
    }
    unsigned char frame[NORND_IPC_MAX_BODY + 4];
    size_t got = 0;
    while (got < 4) {
        ssize_t n = read(fd, frame + got, 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    int64_t body = nornd_ipc_frame_len(frame, 4);
    if (body < 0 || (size_t)body + 4 > sizeof(frame)) { close(fd); return -1; }
    while (got < (size_t)body + 4) {
        ssize_t n = read(fd, frame + got, (size_t)body + 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    close(fd);
    nornd_ipc_resp_t resp;
    size_t consumed = 0;
    if (nornd_ipc_decode_resp(frame, got, &resp, &consumed) != 0) return -1;
    if (!resp.ok) {
        if (resp.has_err && resp.err[0])
            fprintf(stderr, "norn: %s\n", resp.err);
        return -1;
    }
    return 0;
}

/* ipc_round_trip with a binary key (for ops like peer-public that take a
 * 20-byte node-id). Sends req.key=klen bytes; reads a binary value into val. */
static int ipc_round_trip_key(const char *op, const unsigned char *key, size_t klen,
                              unsigned char *val, size_t *vlen, size_t cap) {
    char pbuf[512];
    const char *path = nornd_socket_path(pbuf, sizeof(pbuf));
    int fd = ipc_connect(path);
    if (fd < 0) {
        fprintf(stderr, "norn: cannot reach nornd at %s. Is nornd running?\n", path);
        return -1;
    }
    nornd_ipc_req_t req;
    memset(&req, 0, sizeof(req));
    strcpy(req.op, op);
    if (klen > sizeof(req.key)) klen = sizeof(req.key);
    memcpy(req.key, key, klen);
    req.klen = klen;
    unsigned char wire[NORND_IPC_MAX_BODY + 4];
    int wlen = nornd_ipc_encode_req(&req, wire, sizeof(wire));
    if (wlen < 0 || write(fd, wire, (size_t)wlen) != wlen) {
        close(fd);
        return -1;
    }
    unsigned char frame[NORND_IPC_MAX_BODY + 4];
    size_t got = 0;
    while (got < 4) {
        ssize_t n = read(fd, frame + got, 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    int64_t body = nornd_ipc_frame_len(frame, 4);
    if (body < 0 || (size_t)body + 4 > sizeof(frame)) { close(fd); return -1; }
    while (got < (size_t)body + 4) {
        ssize_t n = read(fd, frame + got, (size_t)body + 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    close(fd);
    nornd_ipc_resp_t resp;
    size_t consumed = 0;
    if (nornd_ipc_decode_resp(frame, got, &resp, &consumed) != 0) return -1;
    if (!resp.ok) {
        if (resp.has_err && resp.err[0])
            fprintf(stderr, "norn: %s\n", resp.err);
        return -1;
    }
    size_t n = resp.vlen;
    if (n > cap) n = cap;
    memcpy(val, resp.val, n);
    *vlen = n;
    return 0;
}

/* ipc_round_trip with a binary key + binary value, returning a binary response.
 * Used by bep44-set/put: sends name+value, receives the 20-byte DHT key. */
static int ipc_round_trip_kv_bin(const char *op,
                                 const unsigned char *key, size_t klen,
                                 const unsigned char *value, size_t vlen,
                                 unsigned char *resp_val, size_t *resp_vlen,
                                 size_t resp_cap) {
    char pbuf[512];
    const char *path = nornd_socket_path(pbuf, sizeof(pbuf));
    int fd = ipc_connect(path);
    if (fd < 0) {
        fprintf(stderr, "norn: cannot reach nornd at %s. Is nornd running?\n", path);
        return -1;
    }
    nornd_ipc_req_t req;
    memset(&req, 0, sizeof(req));
    strcpy(req.op, op);
    if (key && klen) {
        if (klen > sizeof(req.key)) klen = sizeof(req.key);
        memcpy(req.key, key, klen);
        req.klen = klen;
    }
    if (value && vlen) {
        if (vlen > sizeof(req.val)) vlen = sizeof(req.val);
        memcpy(req.val, value, vlen);
        req.vlen = vlen;
        req.has_val = 1;
    }
    unsigned char wire[NORND_IPC_MAX_BODY + 4];
    int wlen = nornd_ipc_encode_req(&req, wire, sizeof(wire));
    if (wlen < 0 || write(fd, wire, (size_t)wlen) != wlen) {
        close(fd);
        return -1;
    }
    unsigned char frame[NORND_IPC_MAX_BODY + 4];
    size_t got = 0;
    while (got < 4) {
        ssize_t n = read(fd, frame + got, 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    int64_t body = nornd_ipc_frame_len(frame, 4);
    if (body < 0 || (size_t)body + 4 > sizeof(frame)) { close(fd); return -1; }
    while (got < (size_t)body + 4) {
        ssize_t n = read(fd, frame + got, (size_t)body + 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    close(fd);
    nornd_ipc_resp_t resp;
    size_t consumed = 0;
    if (nornd_ipc_decode_resp(frame, got, &resp, &consumed) != 0) return -1;
    if (!resp.ok) {
        if (resp.has_err && resp.err[0])
            fprintf(stderr, "norn: %s\n", resp.err);
        return -1;
    }
    size_t n = resp.vlen;
    if (n > resp_cap) n = resp_cap;
    memcpy(resp_val, resp.val, n);
    *resp_vlen = n;
    return 0;
}

/* ipc_round_trip variant that sends op + key + val + expect (the latter three
 * optional). Returns 0 on success and fills resp_val/resp_vlen; -1 on error. */
static int ipc_round_trip_kv_expect(const char *op,
                                    const unsigned char *key, size_t klen,
                                    const unsigned char *val, size_t vlen,
                                    const unsigned char *expect, size_t elen,
                                    unsigned char *resp_val, size_t *resp_vlen,
                                    size_t resp_cap) {
    char pbuf[512];
    const char *path = nornd_socket_path(pbuf, sizeof(pbuf));
    int fd = ipc_connect(path);
    if (fd < 0) {
        fprintf(stderr, "norn: cannot reach nornd at %s. Is nornd running?\n", path);
        return -1;
    }
    nornd_ipc_req_t req;
    memset(&req, 0, sizeof(req));
    strcpy(req.op, op);
    if (key && klen) {
        if (klen > sizeof(req.key)) klen = sizeof(req.key);
        memcpy(req.key, key, klen);
        req.klen = klen;
    }
    if (val && vlen) {
        if (vlen > sizeof(req.val)) vlen = sizeof(req.val);
        memcpy(req.val, val, vlen);
        req.vlen = vlen;
        req.has_val = 1;
    }
    if (expect && elen) {
        if (elen > sizeof(req.expect)) elen = sizeof(req.expect);
        memcpy(req.expect, expect, elen);
        req.elen = elen;
        req.has_expect = 1;
    }
    unsigned char wire[NORND_IPC_MAX_BODY + 4];
    int wlen = nornd_ipc_encode_req(&req, wire, sizeof(wire));
    if (wlen < 0 || write(fd, wire, (size_t)wlen) != wlen) {
        close(fd);
        return -1;
    }
    unsigned char frame[NORND_IPC_MAX_BODY + 4];
    size_t got = 0;
    while (got < 4) {
        ssize_t n = read(fd, frame + got, 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    int64_t body = nornd_ipc_frame_len(frame, 4);
    if (body < 0 || (size_t)body + 4 > sizeof(frame)) { close(fd); return -1; }
    while (got < (size_t)body + 4) {
        ssize_t n = read(fd, frame + got, (size_t)body + 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    close(fd);
    nornd_ipc_resp_t resp;
    size_t consumed = 0;
    if (nornd_ipc_decode_resp(frame, got, &resp, &consumed) != 0) return -1;
    if (!resp.ok) {
        if (resp.has_err && resp.err[0])
            fprintf(stderr, "norn: %s\n", resp.err);
        return -1;
    }
    size_t n = resp.vlen;
    if (n > resp_cap) n = resp_cap;
    memcpy(resp_val, resp.val, n);
    *resp_vlen = n;
    return 0;
}

static int do_node_secret(int argc, char **argv) {
    static struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 3;
    while ((opt = getopt_long(argc, argv, "+h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: norn node secret\n"
                       "\n"
                       "Print the nornd node's Ed25519 secret key (64 hex bytes).\n"
                       "Requires a running nornd daemon.\n");
                return 0;
            default:
                return 1;
        }
    }
    unsigned char val[64];
    size_t vlen = 0;
    if (ipc_round_trip("node-secret", val, &vlen, sizeof(val)) != 0) return 1;
    if (vlen != 64) {
        fprintf(stderr, "norn node secret: unexpected response length\n");
        return 1;
    }
    for (size_t i = 0; i < 64; i++) printf("%02x", val[i]);
    printf("\n");
    return 0;
}

static int do_node_public(int argc, char **argv) {
    static struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 3;
    while ((opt = getopt_long(argc, argv, "+h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: norn node public\n"
                       "\n"
                       "Print the nornd node's Ed25519 public key (32 hex bytes).\n"
                       "Requires a running nornd daemon.\n");
                return 0;
            default:
                return 1;
        }
    }
    unsigned char val[32];
    size_t vlen = 0;
    if (ipc_round_trip("node-public", val, &vlen, sizeof(val)) != 0) return 1;
    if (vlen != 32) {
        fprintf(stderr, "norn node public: unexpected response length\n");
        return 1;
    }
    printf("%s", col_magenta());
    for (size_t i = 0; i < 32; i++) printf("%02x", val[i]);
    printf("%s\n", col_reset());
    return 0;
}

/* `norn peer public <node-id>` — print a peer's Ed25519 pubkey (64 hex),
 * resolved by 40-hex DHT node-id from the local routing table (norn "pk").
 * Only norn peers expose a pubkey; vanilla Mainline nodes don't. */
static int do_peer_public(const char *nodeid_hex) {
    if (!nodeid_hex || strlen(nodeid_hex) != 40) {
        fprintf(stderr, "norn peer public: node-id must be 40 hex chars\n");
        return 2;
    }
    unsigned char node_id[20];
    if (sodium_hex2bin(node_id, sizeof(node_id), nodeid_hex, 40, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "norn peer public: bad 40-hex node-id\n");
        return 2;
    }
    unsigned char pk[32];
    size_t plen = 0;
    if (ipc_round_trip_key("peer-public", node_id, 20, pk, &plen, sizeof(pk)) != 0)
        return 1;
    if (plen != 32) {
        fprintf(stderr, "norn peer public: unexpected response length\n");
        return 1;
    }
    printf("%s", col_magenta());
    for (size_t i = 0; i < 32; i++) printf("%02x", pk[i]);
    printf("%s\n", col_reset());
    return 0;
}

static int do_node_id(int argc, char **argv) {
    static struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 3;
    while ((opt = getopt_long(argc, argv, "+h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: norn node id\n"
                       "\n"
                       "Print the nornd node's DHT node id (20 hex bytes / 40 chars).\n"
                       "This is SHA1(\"k\" || pubkey), the address used in the DHT.\n"
                       "Requires a running nornd daemon.\n");
                return 0;
            default:
                return 1;
        }
    }
    unsigned char val[20];
    size_t vlen = 0;
    if (ipc_round_trip("node-id", val, &vlen, sizeof(val)) != 0) return 1;
    if (vlen != 20) {
        fprintf(stderr, "norn node id: unexpected response length\n");
        return 1;
    }
    printf("%s", col_magenta());
    for (size_t i = 0; i < 20; i++) printf("%02x", val[i]);
    printf("%s\n", col_reset());
    return 0;
}

/* `norn node <start|restart>` — drive the nornd systemd unit. Defaults to the
 * per-user unit (`systemctl --user`); `--system` targets the system unit. Execs
 * systemctl directly so its exit status is the verb's exit status. */
static int do_node_unit_action(const char *action, int argc, char **argv) {
    int system_node = 0;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--system") == 0) {
            system_node = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: norn node %s [--system]\n"
                   "\n"
                   "%s the nornd daemon via systemd.\n"
                   "\n"
                   "Options:\n"
                   "  --system          System nornd (default: user)\n"
                   "  --help            Show this help\n",
                   action, action[0] == 's' ? "Start" : "Restart");
            return 0;
        } else {
            fprintf(stderr, "norn node %s: unknown option: %s\n", action, argv[i]);
            return 1;
        }
    }
    const char *args[6];
    int na = 0;
    args[na++] = "systemctl";
    if (!system_node)
        args[na++] = "--user";
    args[na++] = action;
    args[na++] = "nornd";
    args[na] = NULL;
    execvp("systemctl", (char *const *)args);
    /* If execvp returns, an error occurred. */
    fprintf(stderr, "norn node %s: failed to run systemctl: %s\n", action, strerror(errno));
    return 1;
}

static int do_node_start(int argc, char **argv) {
    return do_node_unit_action("start", argc, argv);
}

static int do_node_restart(int argc, char **argv) {
    return do_node_unit_action("restart", argc, argv);
}

static int do_node_status(int argc, char **argv) {
    (void)argc; (void)argv;
    unsigned char buf[4096];
    size_t vlen = 0;
    if (ipc_round_trip("node-stats", buf, &vlen, sizeof(buf)) != 0) {
        fprintf(stderr, "nornd is not running\n");
        return 1;
    }
    /* nornd returns a recfile; colorize+align on tty, plain on pipe. */
    print_recfile_pretty(buf, vlen);
    return 0;
}

/* `norn node set <name> <value>` — store a local (non-replicated) key on this
 * node, queryable by peers via `peer get`. Not DHT, not cluster KV. */
static int do_node_set(int argc, char **argv) {
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: norn node set <name> <value>\n"
                   "\n"
                   "Store a local key on this node. The key is queryable by a\n"
                   "peer via `norn peer get <node-id> <name>`; it is not stored\n"
                   "in the DHT or the cluster KV, and is only available while\n"
                   "this node is online and reachable.\n");
            return 0;
        }
    }
    if (argc < 5) {
        fprintf(stderr, "Usage: norn node set <name> <value>\n");
        return 1;
    }
    if (ipc_round_trip_kv("node-set", argv[3], argv[4]) != 0) return 1;
    return 0;
}

static int do_node_log(int argc, char **argv) {
    /* Default: 50 lines from the user nornd. Use --system for the system node. */
    int system_node = 0;
    int follow = 0;
    int lines = 50;
    /* Parse optional flags. */
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--system") == 0)
            system_node = 1;
        else if (strcmp(argv[i], "--follow") == 0 || strcmp(argv[i], "-f") == 0)
            follow = 1;
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            lines = atoi(argv[++i]);
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: norn node log [OPTIONS]\n"
                   "\n"
                   "Show nornd daemon logs.\n"
                   "\n"
                   "Options:\n"
                   "  --system          System nornd (default: user)\n"
                   "  -n <lines>        Number of lines (default: 50)\n"
                   "  -f, --follow      Follow new log entries\n"
                   "  --help            Show this help\n");
            return 0;
        } else {
            fprintf(stderr, "norn node log: unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    char lines_buf[16];
    snprintf(lines_buf, sizeof(lines_buf), "%d", lines);
    const char *journalctl_args[16];
    int na = 0;
    journalctl_args[na++] = "journalctl";
    if (system_node)
        journalctl_args[na++] = "-u";
    else {
        journalctl_args[na++] = "--user";
        journalctl_args[na++] = "-u";
    }
    journalctl_args[na++] = "nornd";
    journalctl_args[na++] = "-n";
    journalctl_args[na++] = lines_buf;
    if (follow)
        journalctl_args[na++] = "--follow";
    journalctl_args[na] = NULL;
    execvp("journalctl", (char *const *)journalctl_args);
    /* If execvp returns, an error occurred. */
    fprintf(stderr, "norn node log: failed to run journalctl: %s\n", strerror(errno));
    return 1;
}

/* `norn bep44 get <node-id> <name>` — retrieve a named mutable record.
 * Pure IPC: sends node-id + name to nornd, which resolves the pubkey,
 * checks the publog + dhtstore locally, and returns the value. */
static int do_get(int argc, char **argv) {
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 2;
    while ((opt = getopt_long(argc, argv, "+h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                fprintf(stdout, "Usage: %s bep44 get <node-id> <name>\n", prog_name);
                fprintf(stdout, "\n");
                fprintf(stdout, "Retrieve a named mutable record from the DHT (BEP-44).\n");
                fprintf(stdout, "The publisher's pubkey is resolved from the 40-hex\n");
                fprintf(stdout, "DHT node-id via the running nornd.\n");
                fprintf(stdout, "\n");
                fprintf(stdout, "Arguments:\n");
                fprintf(stdout, "  <node-id>       40-hex DHT node id of the publisher\n");
                fprintf(stdout, "  <name>          Record name (the salt)\n");
                fprintf(stdout, "  --help          Show this help\n");
                return 0;
            default:
                return 1;
        }
    }

    if (optind + 1 >= argc) {
        fprintf(stderr, "ERROR: missing <node-id> and/or <name>\n");
        fprintf(stderr, "Usage: %s bep44 get <node-id> <name>\n", prog_name);
        return 1;
    }

    const char *nodeid_str = argv[optind];
    const char *name = argv[optind + 1];

    if (strlen(nodeid_str) != 40) {
        fprintf(stderr, "ERROR: node-id must be 40 hex characters (got %zu)\n",
                strlen(nodeid_str));
        return 1;
    }
    unsigned char node_id[20];
    if (sodium_hex2bin(node_id, sizeof(node_id), nodeid_str, 40, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "ERROR: invalid hex in node-id\n");
        return 1;
    }

    /* Send node-id + name to nornd; nornd resolves pubkey, checks publog +
     * dhtstore, returns the value. */
    unsigned char val[4096];
    size_t vlen = 0;
    if (ipc_round_trip_kv_bin("bep44-get", node_id, 20,
                              (const unsigned char *)name, strlen(name),
                              val, &vlen, sizeof(val)) != 0) {
        return 1;
    }
    fwrite(val, 1, vlen, stdout);
    fputc('\n', stdout);
    return 0;
}

/* `norn put <value>` — publish an immutable BEP-44 record (content-addressed:
 * key = SHA1(bencode(value))). Returns the 40-hex hash to retrieve it by. */
static int do_put_immutable(int argc, char **argv) {
    static struct option long_options[] = {
        {"key", required_argument, 0, 'k'},
        {"timeout", required_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 2;
    while ((opt = getopt_long(argc, argv, "+k:t:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'k': key_file = optarg; break;
            case 't':
                timeout_ms = atoi(optarg);
                if (timeout_ms <= 0) { fprintf(stderr, "ERROR: Invalid timeout: %s\n", optarg); return 1; }
                break;
            case 'h':
                printf("Usage: %s put [OPTIONS] <value>\n", prog_name);
                printf("\n");
                printf("Publish an immutable record to the DHT (BEP-44). The key is\n");
                printf("SHA1(bencode(value)) — content-addressed, self-verifying, no\n");
                printf("signature. Retrieve with `norn get <hash>`.\n");
                printf("\nOptions:\n");
                printf("  --key <path>      Key file path\n");
                printf("  --timeout <ms>    Query timeout (default: %d)\n", DEFAULT_TIMEOUT);
                printf("  --help            Show this help\n");
                return 0;
            default: return 1;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "ERROR: missing <value>\n");
        fprintf(stderr, "Usage: %s put <value>\n", prog_name);
        return 1;
    }
    const char *value = argv[optind];
    size_t value_len = strlen(value);
    if (value_len > MAX_VALUE_SIZE) {
        fprintf(stderr, "ERROR: Value too large (%zu > %d bytes)\n", value_len, MAX_VALUE_SIZE);
        return 1;
    }

    /* Send to nornd via IPC; nornd enqueues an async DHT publish, logs to
     * publog, and returns the 20-byte key. No blocking. */
    unsigned char key[20];
    size_t klen = 0;
    if (ipc_round_trip_kv_bin("bep44-put", NULL, 0,
                              (const unsigned char *)value, value_len,
                              key, &klen, sizeof(key)) != 0) {
        return 1;
    }
    if (klen != 20) {
        fprintf(stderr, "norn bep44 put: unexpected response length\n");
        return 1;
    }
    printf("%s", col_magenta());
    for (int i = 0; i < 20; i++) printf("%02x", key[i]);
    printf("%s\n", col_reset());
    return 0;
}

/* `norn bep44 get <hash>` (immutable) — retrieve a content-addressed record
 * by its 40-hex SHA1 key. Pure IPC via bep44-get-immutable. */
static int do_get_immutable(int argc, char **argv) {
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 2;
    while ((opt = getopt_long(argc, argv, "+h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s bep44 get <hash>\n", prog_name);
                printf("\n");
                printf("Retrieve an immutable record from the DHT (BEP-44) by its\n");
                printf("40-hex SHA1 content hash.\n");
                printf("  --help            Show this help\n");
                return 0;
            default: return 1;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "ERROR: missing <hash>\n");
        fprintf(stderr, "Usage: %s bep44 get <hash>\n", prog_name);
        return 1;
    }
    const char *hash_str = argv[optind];
    if (strlen(hash_str) != 40) {
        fprintf(stderr, "ERROR: hash must be 40 hex characters (got %zu)\n", strlen(hash_str));
        return 1;
    }
    unsigned char key[20];
    if (sodium_hex2bin(key, sizeof(key), hash_str, 40, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "ERROR: invalid hex in hash\n");
        return 1;
    }

    /* Ask nornd for the value; nornd checks dhtstore + publog. */
    unsigned char val[4096];
    size_t vlen = 0;
    if (ipc_round_trip_key("bep44-get-immutable", key, 20, val, &vlen, sizeof(val)) != 0) {
        return 1;
    }
    fwrite(val, 1, vlen, stdout);
    fputc('\n', stdout);
    return 0;
}

/* `norn bep44 set <name> <value>` — publish a salted mutable record.
 * The name is the BEP-44 salt (a public label, not a secret); the record is
 * addressed by target = SHA1("k" ‖ pubkey ‖ name). One keypair, many names. */
static int do_set(int argc, char **argv) {
    static struct option long_options[] = {
        {"key", required_argument, 0, 'k'},
        {"seq", required_argument, 0, 's'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;

    optind = 2;

    while ((opt = getopt_long(argc, argv, "+k:s:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'k':
                key_file = optarg;
                break;
            case 'h':
                fprintf(stdout, "Usage: %s bep44 set [OPTIONS] <name> <value>\n", prog_name);
                fprintf(stdout, "\n");
                fprintf(stdout, "Publish a signed mutable record to the DHT (BEP-44).\n");
                fprintf(stdout, "The record is named by <name> (the BEP-44 salt); prints\n");
                fprintf(stdout, "the DHT key (SHA1 hash) it is stored under. Retrieve it\n");
                fprintf(stdout, "with `norn bep44 get <your-node-id> <name>`.\n");
                fprintf(stdout, "\n");
                fprintf(stdout, "Arguments:\n");
                fprintf(stdout, "  <name>          Record name (the salt; one keypair, many records)\n");
                fprintf(stdout, "  <value>         Value to store (max %d bytes)\n", MAX_VALUE_SIZE);
                fprintf(stdout, "\n");
                fprintf(stdout, "Options:\n");
                fprintf(stdout, "  --key <path>    Key file path\n");
                fprintf(stdout, "  --help          Show this help\n");
                return 0;
            default:
                return 1;
        }
    }

    if (optind + 1 >= argc) {
        fprintf(stderr, "ERROR: missing <name> and/or <value>\n");
        fprintf(stderr, "Usage: %s bep44 set <name> <value>\n", prog_name);
        return 1;
    }

    const char *name = argv[optind];
    const char *value = argv[optind + 1];
    size_t value_len = strlen(value);
    if (value_len > MAX_VALUE_SIZE) {
        fprintf(stderr, "ERROR: Value too large (%zu > %d bytes)\n", value_len, MAX_VALUE_SIZE);
        return 1;
    }

    /* Send to nornd via IPC; nornd signs with the node keypair, enqueues an
     * async DHT publish, logs to publog, and returns the 20-byte key. No
     * blocking — the daemon's event loop publishes during norn_tick. */
    unsigned char key[20];
    size_t klen = 0;
    if (ipc_round_trip_kv_bin("bep44-set", (const unsigned char *)name, strlen(name),
                              (const unsigned char *)value, value_len,
                              key, &klen, sizeof(key)) != 0) {
        return 1;
    }
    if (klen != 20) {
        fprintf(stderr, "norn bep44 set: unexpected response length\n");
        return 1;
    }
    printf("%s", col_magenta());
    for (int i = 0; i < 20; i++) printf("%02x", key[i]);
    printf("%s\n", col_reset());
    return 0;
}

/* `norn bep44 <get|set> …` — namespaced direct-DHT BEP-44 verbs. Reuses the
 * get/set handlers, which expect the verb at argv[1]; present them a shifted
 * view that drops the "bep44" token. `argv[sub_idx]` is the sub-verb. */
static void bep44_help(FILE *out, const char *prog) {
    fprintf(out, "Usage: %s bep44 <get|set|put|list|del> [ARGS...]\n", prog);
    fprintf(out, "\nSubcommands (mutable — signed, named):\n"
            "  set <name> <value>     Publish a named mutable record (keyed by your pubkey)\n"
            "  get <node-id> <name>   Retrieve a named mutable record (by publisher node-id)\n"
            "\nSubcommands (immutable — content-addressed):\n"
            "  put <value>            Publish an immutable record; prints the content hash\n"
            "  get <hash>             Retrieve an immutable record by its 40-hex SHA1 hash\n"
            "\nSubcommands (enumeration / management):\n"
            "  list                   List DHT records (published + held) as a TSV table\n"
            "  del <key>              Delete a held DHT record by its 40-hex key\n");
}

static int do_bep44(int argc, char **argv, int sub_idx) {
    if (sub_idx >= argc) {
        bep44_help(stderr, prog_name);
        return 1;
    }
    const char *sub = argv[sub_idx];
    if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0 || strcmp(sub, "help") == 0) {
        bep44_help(stdout, prog_name);
        return 0;
    }
    int n = argc - sub_idx + 1; /* prog + (verb..end) */
    char **view = malloc(sizeof(char *) * (size_t)(n + 1));
    if (!view) {
        fprintf(stderr, "ERROR: Out of memory\n");
        return 1;
    }
    view[0] = argv[0];
    for (int i = sub_idx; i < argc; i++) view[1 + (i - sub_idx)] = argv[i];
    view[n] = NULL;

    int rc;
    if (strcmp(sub, "put") == 0) {
        rc = do_put_immutable(n, view);
    } else if (strcmp(sub, "set") == 0) {
        rc = do_set(n, view);
    } else if (strcmp(sub, "list") == 0) {
        /* `norn bep44 list` — enumerate the DHT records this node is holding. */
        unsigned char buf[4096];
        size_t vlen = 0;
        if (ipc_round_trip("bep44-list", buf, &vlen, sizeof(buf)) != 0) {
            fprintf(stderr, "nornd is not running\n");
            rc = 1;
        } else {
            print_tsv_pretty(buf, vlen);
            rc = 0;
        }
    } else if (strcmp(sub, "del") == 0) {
        /* `norn bep44 del <key>` — delete a held DHT record by 40-hex key. */
        if (sub_idx + 1 >= argc) {
            fprintf(stderr, "Usage: %s bep44 del <key>\n", prog_name);
            free(view);
            return 1;
        }
        const char *tgt_str = argv[sub_idx + 1];
        if (strlen(tgt_str) != 40) {
            fprintf(stderr, "norn bep44 del: key must be 40 hex chars\n");
            free(view);
            return 2;
        }
        unsigned char target[20];
        if (sodium_hex2bin(target, sizeof(target), tgt_str, 40, NULL, NULL, NULL) != 0) {
            fprintf(stderr, "norn bep44 del: bad 40-hex key\n");
            free(view);
            return 2;
        }
        unsigned char dummy[1];
        size_t dl = 0;
        if (ipc_round_trip_key("bep44-del", target, 20, dummy, &dl, sizeof(dummy)) != 0)
            rc = 1;
        else
            rc = 0;
    } else if (strcmp(sub, "get") == 0) {
        /* Disambiguate by positional arg count: `get <node-id> <name>` (two
         * args) is mutable; `get <hash>` (one arg) is immutable. */
        int pos = 0;
        for (int i = sub_idx + 1; i < argc; i++)
            if (argv[i][0] != '-') pos++;
        if (pos >= 2)
            rc = do_get(n, view);          /* mutable: <node-id> <name> */
        else
            rc = do_get_immutable(n, view);/* immutable: <hash> */
    } else {
        fprintf(stderr, "ERROR: Unknown bep44 subcommand: %s\n", sub);
        fprintf(stderr, "Usage: %s bep44 <get|set|put|list> ...\n", prog_name);
        rc = 1;
    }
    free(view);
    return rc;
}

/* `norn peer <list|connect|get|cat> …` */
static int do_peer(int argc, char **argv, int optind) {
    if (optind >= argc)
        goto peer_help;
    const char *sub = argv[optind];
    int sub_argc = argc - optind;
    char **sub_argv = argv + optind;

    if (strcmp(sub, "list") == 0) {
        unsigned char buf[65536];
        size_t vlen = 0;
        if (ipc_round_trip("peer-list", buf, &vlen, sizeof(buf)) != 0) {
            fprintf(stderr, "nornd is not running\n");
            return 1;
        }
        /* nornd returns a TSV (header + one row per DHT node). Colorize+align
         * on tty, plain passthrough when piped. */
        print_tsv_pretty(buf, vlen);
        return 0;
    } else if (strcmp(sub, "connect") == 0) {
        if (sub_argc < 2) {
            fprintf(stderr, "Usage: %s peer connect <pubkey[@host:port]>\n", prog_name);
            return 1;
        }
        /* IPC peer-fetch with "connect" verb — dials and reports success. */
        unsigned char dummy[1];
        size_t dlen = 0;
        if (ipc_round_trip_kv_expect("peer-fetch",
                (const unsigned char *)sub_argv[1], strlen(sub_argv[1]),
                (const unsigned char *)"connect", 7,
                NULL, 0, dummy, &dlen, sizeof(dummy)) != 0)
            return 1;
        return 0;
    } else if (strcmp(sub, "public") == 0) {
        /* `norn peer public <node-id>` — print a peer's Ed25519 pubkey. */
        if (sub_argc < 2) {
            fprintf(stderr, "Usage: %s peer public <node-id>\n", prog_name);
            return 1;
        }
        return do_peer_public(sub_argv[1]);
    } else if (strcmp(sub, "get") == 0) {
        /* `norn peer get <node-id> <key>` — resolve the peer by 40-hex DHT node
         * id via the daemon, dial it, and fetch a served-KV value. */
        if (sub_argc < 3) {
            fprintf(stderr, "Usage: %s peer get <node-id> <key>\n", prog_name);
            return 1;
        }
        unsigned char body[NORND_IPC_MAX_VAL];
        size_t blen = sizeof(body);
        if (ipc_round_trip_kv_expect("peer-fetch",
                (const unsigned char *)sub_argv[1], strlen(sub_argv[1]),
                (const unsigned char *)"get", 3,
                (const unsigned char *)sub_argv[2], strlen(sub_argv[2]),
                body, &blen, sizeof(body)) != 0)
            return 1;
        fwrite(body, 1, blen, stdout);
        return 0;
    } else if (strcmp(sub, "cat") == 0) {
        if (sub_argc < 3) {
            fprintf(stderr, "Usage: %s peer cat <pubkey[@host:port]> <hash>\n",
                    prog_name);
            return 1;
        }
        unsigned char body[NORND_IPC_MAX_VAL];
        size_t blen = sizeof(body);
        if (ipc_round_trip_kv_expect("peer-fetch",
                (const unsigned char *)sub_argv[1], strlen(sub_argv[1]),
                (const unsigned char *)"cat", 3,
                (const unsigned char *)sub_argv[2], strlen(sub_argv[2]),
                body, &blen, sizeof(body)) != 0)
            return 1;
        fwrite(body, 1, blen, stdout);
        return 0;
    } else if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0 || strcmp(sub, "help") == 0) {
        goto peer_help;
    }
    fprintf(stderr, "ERROR: Unknown peer subcommand: %s\n", sub);
    return 1;

peer_help:
    fprintf(stdout, "Usage: %s peer <subcommand> [ARGS...]\n", prog_name);
    fprintf(stdout, "\nSubcommands:\n");
    fprintf(stdout, "  list                   List known peers\n");
    fprintf(stdout, "  public <node-id>       Print a peer's Ed25519 pubkey\n");
    fprintf(stdout, "  connect <pubkey[@h]>   Dial a remote peer\n");
    fprintf(stdout, "  get <node-id> <key>    Fetch a served-KV value from a peer\n");
    fprintf(stdout, "  cat <pubkey[@h]> <h>   Fetch a served-KV blob\n");
    fprintf(stdout, "\nSee `%s --help` for top-level options.\n", prog_name);
    return optind < argc ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 1;
    }
    
    /* Check for --help or -h before command */
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(stdout);
        return 0;
    }
    
    /* Parse global options first */
    static struct option long_options[] = {
        {"key", required_argument, 0, 'k'},
        {"port", required_argument, 0, 'p'},
        {"timeout", required_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    
    while ((opt = getopt_long(argc, argv, "+k:p:t:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'k':
                key_file = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                if (port <= 0 || port > 65535) {
                    fprintf(stderr, "ERROR: Invalid port: %s\n", optarg);
                    return 1;
                }
                break;
            case 't':
                timeout_ms = atoi(optarg);
                if (timeout_ms <= 0) {
                    fprintf(stderr, "ERROR: Invalid timeout: %s\n", optarg);
                    return 1;
                }
                break;
            case 'h':
                usage(stdout);
                return 0;
            case '?':
            default:
                usage(stderr);
                return 1;
        }
    }
    
    if (optind >= argc) {
        usage(stderr);
        return 1;
    }
    
    if (sodium_init() < 0) {
        fprintf(stderr, "ERROR: Failed to initialize libsodium\n");
        return 1;
    }
    
    const char *cmd = argv[optind];
    
    if (strcmp(cmd, "version") == 0) {
        return do_version();
    } else if (strcmp(cmd, "bep44") == 0) {
        /* BEP-44 DHT records: norn bep44 <get|set|put> ...
         *   set <name> <value>   — mutable, signed, salted by name
         *   get <node-id> <name> — mutable (by publisher node-id + name)
         *   put <value>          — immutable, content-addressed
         *   get <hash>           — immutable (by content hash) */
        return do_bep44(argc, argv, optind + 1);
    } else if (strcmp(cmd, "cluster") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stdout, "Usage: %s cluster <subcommand> [ARGS...]\n", prog_name);
            fprintf(stdout, "\nSubcommands:\n"
                    "  put <key> <val>    Store a key-value pair\n"
                    "  get <key>          Retrieve a value\n"
                    "  del <key>          Delete a key\n"
                    "  cas <k> <o> <n>    Compare-and-swap\n"
                    "  watch <prefix>     Stream changes\n"
                    "  members            List cluster members\n"
                    "  leader             Show current leader\n"
                    "  status             Cluster health\n");
            return optind + 1 < argc ? 0 : 1;
        }
        return nornd_cli_cluster(argc - optind - 1, argv + optind + 1);
    } else if (strcmp(cmd, "node") == 0) {
        if (optind + 1 < argc) {
            const char *sub = argv[optind + 1];
            if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0 || strcmp(sub, "help") == 0) {
                goto node_help;
            }
            if (strcmp(sub, "start") == 0)
                return do_node_start(argc, argv);
            if (strcmp(sub, "restart") == 0)
                return do_node_restart(argc, argv);
            if (strcmp(sub, "status") == 0)
                return do_node_status(argc, argv);
            if (strcmp(sub, "log") == 0)
                return do_node_log(argc, argv);
            if (strcmp(sub, "public") == 0)
                return do_node_public(argc, argv);
            if (strcmp(sub, "id") == 0)
                return do_node_id(argc, argv);
            if (strcmp(sub, "secret") == 0)
                return do_node_secret(argc, argv);
            if (strcmp(sub, "set") == 0)
                return do_node_set(argc, argv);
        }
node_help:
        fprintf(stdout, "Usage: %s node <subcommand> [ARGS...]\n", prog_name);
        fprintf(stdout, "\nSubcommands:\n"
                "  start          Start the nornd daemon\n"
                "  restart        Restart the nornd daemon\n"
                "  status         Node status (recfile)\n"
                "  log            View daemon logs\n"
                "  id             Print node's DHT node id (40 hex)\n"
                "  public         Print node's Ed25519 public key\n"
                "  secret         Print node's Ed25519 secret key\n"
                "  set            Set a local served-KV key\n");
        fprintf(stdout, "\nSee `%s --help` for top-level options.\n", prog_name);
        return optind + 1 < argc ? 0 : 1;
    } else if (strcmp(cmd, "peer") == 0) {
        return do_peer(argc, argv, optind + 1);
    }

    fprintf(stderr, "ERROR: Unknown command: %s\n", cmd);
    usage(stderr);
    return 1;
}