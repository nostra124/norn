/* SPDX-License-Identifier: MIT */
/**
 * @file cli_cluster.c
 * @brief `norn cluster …` command glue (FEAT-030). See cli_cluster.h.
 *
 * I/O wrapper: resolve the nornd socket, send one request built by client.c,
 * print the formatted reply. `watch` stays connected and prints a line per
 * streamed change. Socket glue — excluded from unit coverage.
 */

#include "cli_cluster.h"
#include "client.h"
#include "ipc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char *socket_path(char *buf, size_t cap) {
    const char *env = getenv("NORN_SOCK");
    if (env && env[0]) return env;
    /* Prefer the per-user daemon socket, fall back to the system nornd socket. */
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
    snprintf(buf, cap, "%s", "/run/nornd/nornd.sock");
    return buf;
}

static int connect_sock(const char *path) {
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

/* Read exactly one length-prefixed frame into buf. Returns body+prefix size,
 * 0 on clean EOF, -1 on error. */
static int read_frame(int fd, unsigned char *buf, size_t cap) {
    size_t got = 0;
    while (got < 4) {
        ssize_t n = read(fd, buf + got, 4 - got);
        if (n == 0) return 0;
        if (n < 0) return -1;
        got += (size_t)n;
    }
    int64_t body = nornd_ipc_frame_len(buf, 4);
    if (body < 0 || (size_t)body + 4 > cap) return -1;
    while (got < (size_t)body + 4) {
        ssize_t n = read(fd, buf + got, (size_t)body + 4 - got);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return (int)((size_t)body + 4);
}

static int print_resp(const nornd_ipc_req_t *req, const unsigned char *frame,
                      int flen) {
    nornd_ipc_resp_t resp;
    size_t consumed = 0;
    if (nornd_ipc_decode_resp(frame, (size_t)flen, &resp, &consumed) != 0) {
        fprintf(stderr, "norn: malformed response from nornd\n");
        return 1;
    }
    char out[NORND_IPC_MAX_VAL + 4096];
    size_t olen = 0;
    int rc = nornd_client_format(req, &resp, out, sizeof(out), &olen);
    if (rc != 0) {
        /* error path: plain, no colorize */
        fwrite(out, 1, olen, stderr);
        return rc;
    }
    /* On a tty, colorize+align structured outputs (recfile/tsv). Raw outputs
     * (get value, watch events) pass through unchanged. */
    int tty = isatty(STDOUT_FILENO);
    const char *op = req->op;
    int structured = strcmp(op, "status") == 0 || strcmp(op, "members") == 0;
    if (tty && structured) {
        const char *cyan = "\033[36m", *bold = "\033[1m", *reset = "\033[0m";
        if (strcmp(op, "status") == 0) {
            /* recfile: align keys, color values */
            size_t i = 0, maxk = 0;
            while (i < olen) {
                size_t eq = i;
                while (eq < olen && out[eq] != '=' && out[eq] != '\n') eq++;
                if (eq < olen && out[eq] == '=' && eq - i > maxk) maxk = eq - i;
                while (i < olen && out[i] != '\n') i++;
                if (i < olen) i++;
            }
            i = 0;
            while (i < olen) {
                size_t eq = i;
                while (eq < olen && out[eq] != '=' && out[eq] != '\n') eq++;
                if (eq < olen && out[eq] == '=') {
                    size_t klen = eq - i;
                    fwrite(cyan, 1, strlen(cyan), stdout);
                    fwrite(out + i, 1, klen, stdout);
                    for (size_t p = klen; p < maxk; p++) fputc(' ', stdout);
                    printf("%s=%s", reset, bold);
                    size_t vs = eq + 1, ve = vs;
                    while (ve < olen && out[ve] != '\n') ve++;
                    fwrite(out + vs, 1, ve - vs, stdout);
                    printf("%s\n", reset);
                    i = ve;
                    if (i < olen) i++;
                } else {
                    size_t eol = i;
                    while (eol < olen && out[eol] != '\n') eol++;
                    fwrite(out + i, 1, eol - i, stdout);
                    if (eol < olen) { fputc('\n', stdout); eol++; }
                    i = eol;
                }
            }
        } else {
            /* members TSV: colorize header + data rows, align columns */
            /* TSV lines: header "index\tnodeid", then "0\t<hex>"… */
            size_t i = 0;
            while (i < olen) {
                size_t eol = i;
                while (eol < olen && out[eol] != '\n') eol++;
                /* find tab */
                size_t tab = i;
                while (tab < eol && out[tab] != '\t') tab++;
                if (tab < eol) {
                    fwrite(cyan, 1, strlen(cyan), stdout);
                    fwrite(out + i, 1, tab - i, stdout);
                    printf("%s\t%s", reset, bold);
                    fwrite(out + tab + 1, 1, eol - tab - 1, stdout);
                    printf("%s\n", reset);
                } else {
                    fwrite(out + i, 1, eol - i, stdout);
                    if (eol < olen) fputc('\n', stdout);
                }
                i = eol;
                if (i < olen) i++;
            }
        }
    } else {
        fwrite(out, 1, olen, stdout);
    }
    return rc;
}

int nornd_cli_cluster(int argc, char **argv) {
    nornd_ipc_req_t req;
    char err[128];
    if (nornd_client_build_req(argc, argv, &req, err, sizeof(err)) != 0) {
        fprintf(stderr, "norn cluster: %s\n", err);
        return 2;
    }

    char pbuf[512];
    const char *path = socket_path(pbuf, sizeof(pbuf));
    int fd = connect_sock(path);
    if (fd < 0) {
        fprintf(stderr, "norn: cannot reach nornd at %s (%s). Is nornd running?\n",
                path, strerror(errno));
        return 1;
    }

    unsigned char wire[NORND_IPC_MAX_BODY + 4];
    int wlen = nornd_ipc_encode_req(&req, wire, sizeof(wire));
    if (wlen < 0 || write(fd, wire, (size_t)wlen) != wlen) {
        fprintf(stderr, "norn: failed to send request\n");
        close(fd);
        return 1;
    }

    int is_watch = strcmp(req.op, "watch") == 0;
    unsigned char frame[NORND_IPC_MAX_BODY + 4];
    int rc = 1;
    int flen = read_frame(fd, frame, sizeof(frame));
    if (flen > 0) {
        rc = print_resp(&req, frame, flen);
        /* watch: keep printing streamed change frames until nornd closes.
         * Flush each one so events surface live even when stdout is a pipe. */
        if (is_watch) fflush(stdout);
        while (is_watch && (flen = read_frame(fd, frame, sizeof(frame))) > 0) {
            print_resp(&req, frame, flen);
            fflush(stdout);
        }
    } else {
        fprintf(stderr, "norn: no response from nornd\n");
    }
    close(fd);
    return rc;
}
