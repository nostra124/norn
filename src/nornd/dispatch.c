/**
 * @file dispatch.c
 * @brief Pure IPC request→response mapping (FEAT-029). See dispatch.h.
 */

#include "dispatch.h"

#include <string.h>

static void fail(nornd_ipc_resp_t *r, const char *msg) {
    r->ok = 0;
    r->has_err = 1;
    size_t n = strlen(msg);
    if (n >= sizeof(r->err)) n = sizeof(r->err) - 1; /* LCOV_EXCL_BR_LINE */
    memcpy(r->err, msg, n);
    r->err[n] = '\0';
}

static void ok_empty(nornd_ipc_resp_t *r) { r->ok = 1; }

static void ok_val(nornd_ipc_resp_t *r, const unsigned char *v, size_t n) {
    r->ok = 1;
    r->has_val = 1;
    memcpy(r->val, v, n);
    r->vlen = n;
}

/* Fill the items list with member pubkeys; returns the count. */
static int fill_members(const nornd_backend_t *be, nornd_ipc_resp_t *r) {
    unsigned char list[NORND_IPC_MAX_ITEMS][NORND_PUBKEY];
    int n = be->members(be->ctx, list, NORND_IPC_MAX_ITEMS);
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        memcpy(r->items[i].data, list[i], NORND_PUBKEY);
        r->items[i].len = NORND_PUBKEY;
    }
    r->n_items = n;
    return n;
}

/* Collect each `peer/<id>/ssh` value as one response item — the fleet's
 * authorized_keys lines. Keydir layout: src/nornd/keydir.h. */
static void authkeys_visit(void *ud, const unsigned char *key, size_t klen,
                           const unsigned char *val, size_t vlen) {
    nornd_ipc_resp_t *r = ud;
    int is_ssh = klen >= 4 && memcmp(key + klen - 4, "/ssh", 4) == 0;
    if (is_ssh && r->n_items < NORND_IPC_MAX_ITEMS && vlen <= NORND_IPC_MAX_ITEM) {
        memcpy(r->items[r->n_items].data, val, vlen);
        r->items[r->n_items].len = vlen;
        r->n_items++;
    }
}

void nornd_dispatch(const nornd_backend_t *be, const nornd_ipc_req_t *req,
                    nornd_ipc_resp_t *resp) {
    memset(resp, 0, sizeof(*resp));
    const char *op = req->op;

    if (strcmp(op, "get") == 0) {
        if (req->klen == 0) {
            fail(resp, "missing key");
            return;
        }
        unsigned char buf[NORND_IPC_MAX_VAL];
        int n = be->get(be->ctx, req->key, req->klen, buf, sizeof(buf));
        if (n < 0)
            fail(resp, "not found");
        else
            ok_val(resp, buf, (size_t)n);
        return;
    }
    if (strcmp(op, "put") == 0) {
        if (req->klen == 0) {
            fail(resp, "missing key");
            return;
        }
        if (!req->has_val) {
            fail(resp, "missing val");
            return;
        }
        if (be->put(be->ctx, req->key, req->klen, req->val, req->vlen) == 0)
            ok_empty(resp);
        else
            fail(resp, "no leader");
        return;
    }
    if (strcmp(op, "del") == 0) {
        if (req->klen == 0) {
            fail(resp, "missing key");
            return;
        }
        if (be->del(be->ctx, req->key, req->klen) == 0)
            ok_empty(resp);
        else
            fail(resp, "no leader");
        return;
    }
    if (strcmp(op, "cas") == 0) {
        if (req->klen == 0) {
            fail(resp, "missing key");
            return;
        }
        if (!req->has_expect || !req->has_val) {
            fail(resp, "cas needs expect and val");
            return;
        }
        unsigned char cur[NORND_IPC_MAX_VAL];
        int n = be->get(be->ctx, req->key, req->klen, cur, sizeof(cur));
        if (n < 0) {
            fail(resp, "cas: key absent");
            return;
        }
        if ((size_t)n != req->elen || memcmp(cur, req->expect, req->elen) != 0) {
            fail(resp, "cas: mismatch");
            return;
        }
        if (be->put(be->ctx, req->key, req->klen, req->val, req->vlen) == 0)
            ok_empty(resp);
        else
            fail(resp, "no leader");
        return;
    }
    if (strcmp(op, "members") == 0) {
        fill_members(be, resp);
        resp->ok = 1;
        return;
    }
    if (strcmp(op, "leader") == 0) {
        const unsigned char *ld = be->leader(be->ctx);
        if (ld)
            ok_val(resp, ld, NORND_PUBKEY);
        else
            fail(resp, "no leader");
        return;
    }
    if (strcmp(op, "status") == 0) {
        unsigned char v[1 + NORND_PUBKEY];
        v[0] = be->is_leader(be->ctx) ? 1 : 0;
        const unsigned char *ld = be->leader(be->ctx);
        size_t vn = 1;
        if (ld) {
            memcpy(v + 1, ld, NORND_PUBKEY);
            vn = 1 + NORND_PUBKEY;
        }
        ok_val(resp, v, vn);
        fill_members(be, resp);
        return;
    }
    if (strcmp(op, "watch") == 0) {
        /* Validate; the daemon turns this into a streaming subscription. */
        ok_empty(resp);
        return;
    }
    if (strcmp(op, "authkeys") == 0) {
        /* Enumerate the fleet's published SSH keys into the item list. */
        be->scan(be->ctx, (const unsigned char *)"peer/", 5, authkeys_visit, resp);
        resp->ok = 1;
        return;
    }
    fail(resp, "unknown op");
}

void nornd_watch_event(nornd_ipc_resp_t *resp, norn_kv_event_t ev,
                       const unsigned char *key, size_t klen,
                       const unsigned char *val, size_t vlen) {
    memset(resp, 0, sizeof(*resp));
    resp->ok = 1;
    const char *kind = ev == NORN_KV_EV_DEL ? "del" : "put";
    size_t kl = strlen(kind);
    memcpy(resp->items[0].data, kind, kl);
    resp->items[0].len = kl;
    if (klen > NORND_IPC_MAX_ITEM) klen = NORND_IPC_MAX_ITEM;
    memcpy(resp->items[1].data, key, klen);
    resp->items[1].len = klen;
    resp->n_items = 2;
    /* DEL carries no value; a PUT value that would overflow the field is
     * dropped (the watcher still learns the key changed). */
    if (ev == NORN_KV_EV_PUT && vlen <= NORND_IPC_MAX_VAL) {
        memcpy(resp->val, val, vlen);
        resp->vlen = vlen;
        resp->has_val = 1;
    }
}
