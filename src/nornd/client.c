/**
 * @file client.c
 * @brief `norn cluster …` CLI client helpers (FEAT-030). See client.h.
 */

#include "client.h"
#include "dispatch.h" /* NORND_PUBKEY */

#include <stdio.h>
#include <string.h>

static void set_err(char *err, size_t cap, const char *msg) {
    if (err && cap) {
        size_t n = strlen(msg);
        if (n >= cap) n = cap - 1;
        memcpy(err, msg, n);
        err[n] = '\0';
    }
}

/* Copy a NUL-terminated arg into a bounded field; -1 if it would overflow. */
static int copy_arg(unsigned char *dst, size_t cap, size_t *len, const char *s) {
    size_t n = strlen(s);
    if (n > cap) return -1;
    memcpy(dst, s, n);
    *len = n;
    return 0;
}

int nornd_client_build_req(int argc, char **argv, nornd_ipc_req_t *req,
                           char *err, size_t errcap) {
    if (argc < 1) {
        set_err(err, errcap, "missing subcommand");
        return -1;
    }
    memset(req, 0, sizeof(*req));
    const char *sub = argv[0];

    if (strcmp(sub, "put") == 0) {
        if (argc < 3) {
            set_err(err, errcap, "usage: cluster put <key> <value>");
            return -1;
        }
        strcpy(req->op, "put");
        if (copy_arg(req->key, sizeof(req->key), &req->klen, argv[1]) != 0 ||
            copy_arg(req->val, sizeof(req->val), &req->vlen, argv[2]) != 0) {
            set_err(err, errcap, "key or value too long");
            return -1;
        }
        req->has_val = 1;
        return 0;
    }
    if (strcmp(sub, "get") == 0 || strcmp(sub, "del") == 0 ||
        strcmp(sub, "watch") == 0) {
        if (argc < 2) {
            set_err(err, errcap, "usage: cluster <get|del|watch> <key>");
            return -1;
        }
        strcpy(req->op, sub);
        if (copy_arg(req->key, sizeof(req->key), &req->klen, argv[1]) != 0) {
            set_err(err, errcap, "key too long");
            return -1;
        }
        return 0;
    }
    if (strcmp(sub, "cas") == 0) {
        if (argc < 4) {
            set_err(err, errcap, "usage: cluster cas <key> <expect> <value>");
            return -1;
        }
        strcpy(req->op, "cas");
        if (copy_arg(req->key, sizeof(req->key), &req->klen, argv[1]) != 0 ||
            copy_arg(req->expect, sizeof(req->expect), &req->elen, argv[2]) != 0 ||
            copy_arg(req->val, sizeof(req->val), &req->vlen, argv[3]) != 0) {
            set_err(err, errcap, "argument too long");
            return -1;
        }
        req->has_expect = 1;
        req->has_val = 1;
        return 0;
    }
    if (strcmp(sub, "members") == 0 || strcmp(sub, "leader") == 0 ||
        strcmp(sub, "status") == 0) {
        strcpy(req->op, sub);
        return 0;
    }
    set_err(err, errcap, "unknown cluster subcommand");
    return -1;
}

/* Append into a bounded cursor; silently stops at capacity. */
typedef struct {
    char *p;
    size_t cap;
    size_t len;
} sink_t;

static void put_bytes(sink_t *s, const void *d, size_t n) {
    if (s->len + n > s->cap) n = s->cap - s->len; /* LCOV_EXCL_BR_LINE */
    memcpy(s->p + s->len, d, n);
    s->len += n;
}
static void put_cstr(sink_t *s, const char *str) { put_bytes(s, str, strlen(str)); }
static void put_hex(sink_t *s, const unsigned char *b, size_t n) {
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        char two[2] = {H[b[i] >> 4], H[b[i] & 0xf]};
        put_bytes(s, two, 2);
    }
}

int nornd_client_format(const nornd_ipc_req_t *req, const nornd_ipc_resp_t *resp,
                        char *out, size_t cap, size_t *outlen) {
    sink_t s = {out, cap, 0};
    if (!resp->ok) {
        put_cstr(&s, "error: ");
        put_cstr(&s, resp->has_err ? resp->err : "request failed");
        put_cstr(&s, "\n");
        *outlen = s.len;
        return 1;
    }
    const char *op = req->op;
    if (strcmp(op, "get") == 0) {
        if (resp->has_val) put_bytes(&s, resp->val, resp->vlen);
        put_cstr(&s, "\n");
    } else if (strcmp(op, "leader") == 0) {
        put_hex(&s, resp->val, resp->vlen);
        put_cstr(&s, "\n");
    } else if (strcmp(op, "members") == 0) {
        for (int i = 0; i < resp->n_items; i++) {
            put_hex(&s, resp->items[i].data, resp->items[i].len);
            put_cstr(&s, "\n");
        }
    } else if (strcmp(op, "status") == 0) {
        put_cstr(&s, resp->vlen > 0 && resp->val[0] == 1 ? "role: leader\n"
                                                         : "role: follower\n");
        if (resp->vlen > 1) {
            put_cstr(&s, "leader: ");
            put_hex(&s, resp->val + 1, resp->vlen - 1);
            put_cstr(&s, "\n");
        }
        char line[32];
        snprintf(line, sizeof(line), "members: %d\n", resp->n_items);
        put_cstr(&s, line);
    } else {
        /* put/del/cas/watch acknowledgement */
        put_cstr(&s, "OK\n");
    }
    *outlen = s.len;
    return 0;
}
