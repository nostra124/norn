/**
 * @file cli_keys.c
 * @brief `norn keys <nodeid>` — resolve a peer's SSH/GPG keys (FEAT-031).
 *
 * Glue: an IPC-backed keydir backend (its `get` does a one-shot cluster KV
 * `get` over the nornd socket), driven through the tested keydir resolvers.
 * Socket I/O — excluded from unit coverage.
 */

#include "cli_keys.h"
#include "dispatch.h"
#include "ipc.h"
#include "keydir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char *socket_path(char *buf, size_t cap) {
    const char *env = getenv("NORN_SOCK");
    if (env && env[0]) return env;
    const char *run = getenv("XDG_RUNTIME_DIR");
    if (run && run[0]) {
        snprintf(buf, cap, "%s/nornd.sock", run);
        return buf;
    }
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(buf, cap, "%s/.config/norn/nornd.sock", home);
    return buf;
}

/* One-shot cluster KV get over the socket. Returns value length or -1. */
static int ipc_get(void *ctx, const unsigned char *k, size_t kl, unsigned char *out,
                   size_t cap) {
    const char *path = (const char *)ctx;
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
    nornd_ipc_req_t req;
    memset(&req, 0, sizeof(req));
    strcpy(req.op, "get");
    memcpy(req.key, k, kl);
    req.klen = kl;
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
        if (n <= 0) {
            close(fd);
            return -1;
        }
        got += (size_t)n;
    }
    int64_t body = nornd_ipc_frame_len(frame, 4);
    if (body < 0 || (size_t)body + 4 > sizeof(frame)) {
        close(fd);
        return -1;
    }
    while (got < (size_t)body + 4) {
        ssize_t n = read(fd, frame + got, (size_t)body + 4 - got);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        got += (size_t)n;
    }
    close(fd);
    nornd_ipc_resp_t resp;
    size_t consumed = 0;
    if (nornd_ipc_decode_resp(frame, got, &resp, &consumed) != 0 || !resp.ok ||
        !resp.has_val || resp.vlen > cap)
        return -1;
    memcpy(out, resp.val, resp.vlen);
    return (int)resp.vlen;
}

static int hex_to_id(const char *s, unsigned char id[NORND_PUBKEY]) {
    if (strlen(s) != 2 * NORND_PUBKEY) return -1;
    for (int i = 0; i < NORND_PUBKEY; i++) {
        int hi = -1, lo = -1;
        char c = s[2 * i], d = s[2 * i + 1];
        hi = (c >= '0' && c <= '9') ? c - '0'
             : (c >= 'a' && c <= 'f') ? c - 'a' + 10
             : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                      : -1;
        lo = (d >= '0' && d <= '9') ? d - '0'
             : (d >= 'a' && d <= 'f') ? d - 'a' + 10
             : (d >= 'A' && d <= 'F') ? d - 'A' + 10
                                      : -1;
        if (hi < 0 || lo < 0) return -1;
        id[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

int nornd_cli_keys(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "usage: norn keys <nodeid-hex>\n");
        return 2;
    }
    unsigned char id[NORND_PUBKEY];
    if (hex_to_id(argv[0], id) != 0) {
        fprintf(stderr, "norn keys: nodeid must be %d hex chars\n", 2 * NORND_PUBKEY);
        return 2;
    }
    char pbuf[512];
    char path[512];
    strncpy(path, socket_path(pbuf, sizeof(pbuf)), sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    nornd_backend_t be;
    memset(&be, 0, sizeof(be));
    be.ctx = path;
    be.get = ipc_get;

    int found = 0;
    char ssh[1024];
    int sn = nornd_keydir_get_ssh(&be, id, ssh, sizeof(ssh));
    if (sn > 0) {
        printf("%s\n", ssh);
        found = 1;
    }
    unsigned char gpg[200000];
    int gn = nornd_keydir_get_gpg(&be, id, gpg, sizeof(gpg));
    if (gn > 0) {
        fwrite(gpg, 1, (size_t)gn, stdout);
        found = 1;
    }
    if (!found) {
        fprintf(stderr, "norn keys: no keys for %s (is nornd running?)\n", argv[0]);
        return 1;
    }
    return 0;
}
