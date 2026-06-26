/**
 * @file ipc.c
 * @brief norn <-> nornd IPC protocol codec (FEAT-027). See ipc.h.
 *
 * Pure: builds/parses bencode bodies (libnorn's bencode.c) and frames them with
 * a 4-byte big-endian length. No sockets. Bounded fixed-capacity structs.
 */

#include "ipc.h"
#include "bencode.h"

#include <stdlib.h>
#include <string.h>

int64_t nornd_ipc_frame_len(const unsigned char *buf, size_t len) {
    if (!buf || len < 4) return -1;
    uint32_t n = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                 ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    return (int64_t)n;
}

/* ---- encode helpers ---- */

static int add_str(bencode_value_t *d, const char *k, const unsigned char *v, size_t n) {
    bencode_value_t *s = bencode_string_new((const char *)v, n);
    if (!s) return -1; /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    if (bencode_dict_add(d, k, s) != 0) { /* LCOV_EXCL_BR_LINE */
        bencode_free(s); /* LCOV_EXCL_LINE */
        return -1;       /* LCOV_EXCL_LINE */
    }
    return 0;
}

static int add_int(bencode_value_t *d, const char *k, int64_t v) {
    bencode_value_t *i = bencode_int_new(v);
    if (!i) return -1; /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    if (bencode_dict_add(d, k, i) != 0) { /* LCOV_EXCL_BR_LINE */
        bencode_free(i); /* LCOV_EXCL_LINE */
        return -1;       /* LCOV_EXCL_LINE */
    }
    return 0;
}

/* Frame an encoded dict body into out. Frees `d` and the encoded buffer. */
static int frame(bencode_value_t *d, int build_rc, unsigned char *out, size_t cap) {
    if (build_rc != 0) { /* LCOV_EXCL_BR_LINE: build only fails on malloc */
        bencode_free(d); /* LCOV_EXCL_LINE */
        return -1;       /* LCOV_EXCL_LINE */
    }
    size_t blen = 0;
    char *enc = bencode_encode(d, &blen);
    bencode_free(d);
    if (!enc) return -1; /* LCOV_EXCL_BR_LINE: encode of a valid tree never fails */
    if (blen > NORND_IPC_MAX_BODY) { /* LCOV_EXCL_BR_LINE: bounded structs never exceed MAX_BODY */
        free(enc);                   /* LCOV_EXCL_LINE */
        return -1;                   /* LCOV_EXCL_LINE */
    }
    if ((size_t)4 + blen > cap) {
        free(enc);
        return -1;
    }
    out[0] = (unsigned char)(blen >> 24);
    out[1] = (unsigned char)(blen >> 16);
    out[2] = (unsigned char)(blen >> 8);
    out[3] = (unsigned char)(blen);
    memcpy(out + 4, enc, blen);
    free(enc);
    return (int)(4 + blen);
}

int nornd_ipc_encode_req(const nornd_ipc_req_t *r, unsigned char *out, size_t cap) {
    if (!r || !out || r->op[0] == '\0') return -1;
    if (r->klen > NORND_IPC_MAX_KEY || r->vlen > NORND_IPC_MAX_VAL ||
        r->elen > NORND_IPC_MAX_VAL)
        return -1;
    bencode_value_t *d = bencode_dict_new();
    if (!d) return -1; /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    int rc = add_str(d, "op", (const unsigned char *)r->op, strlen(r->op));
    if (r->klen) rc |= add_str(d, "key", r->key, r->klen);
    if (r->has_val) rc |= add_str(d, "val", r->val, r->vlen);
    if (r->has_expect) rc |= add_str(d, "expect", r->expect, r->elen);
    if (r->has_id) rc |= add_str(d, "id", r->id, NORND_IPC_ID_BYTES);
    if (r->has_seq) rc |= add_int(d, "seq", r->seq);
    return frame(d, rc, out, cap);
}

int nornd_ipc_encode_resp(const nornd_ipc_resp_t *r, unsigned char *out, size_t cap) {
    if (!r || !out) return -1;
    if (r->vlen > NORND_IPC_MAX_VAL || r->n_items < 0 || r->n_items > NORND_IPC_MAX_ITEMS)
        return -1;
    bencode_value_t *d = bencode_dict_new();
    if (!d) return -1; /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    int rc = add_int(d, "ok", r->ok ? 1 : 0);
    if (r->has_val) rc |= add_str(d, "val", r->val, r->vlen);
    if (r->has_err) rc |= add_str(d, "err", (const unsigned char *)r->err, strlen(r->err));
    if (r->n_items > 0) {
        bencode_value_t *list = bencode_list_new();
        if (!list) return frame(d, -1, out, cap); /* LCOV_EXCL_LINE: malloc failure not unit-tested */
        for (int i = 0; i < r->n_items; i++) {
            bencode_value_t *s =
                bencode_string_new((const char *)r->items[i].data, r->items[i].len);
            if (!s || bencode_list_add(list, s) != 0) { /* LCOV_EXCL_BR_LINE: malloc */
                bencode_free(s);    /* LCOV_EXCL_LINE */
                bencode_free(list); /* LCOV_EXCL_LINE */
                return frame(d, -1, out, cap); /* LCOV_EXCL_LINE */
            }
        }
        if (bencode_dict_add(d, "items", list) != 0) { /* LCOV_EXCL_BR_LINE: malloc */
            bencode_free(list); /* LCOV_EXCL_LINE */
            rc = -1;            /* LCOV_EXCL_LINE */
        }
    }
    return frame(d, rc, out, cap);
}

/* ---- decode helpers ---- */

/* Copy an optional string field into a bounded buffer. Returns 0 (absent or
 * copied), -1 (wrong type or too long). */
static int get_str(const bencode_value_t *d, const char *k, unsigned char *out,
                   size_t cap, size_t *outlen, int *has) {
    bencode_value_t *v = bencode_dict_get(d, k);
    if (!v) {
        if (has) *has = 0;
        *outlen = 0;
        return 0;
    }
    if (v->type != BENCODE_STRING || v->val.str_val.len > cap) return -1;
    memcpy(out, v->val.str_val.data, v->val.str_val.len);
    *outlen = v->val.str_val.len;
    if (has) *has = 1;
    return 0;
}

/* Decode a frame body into a dict, with all the framing/length checks.
 * Returns the dict (caller frees) or NULL; sets *consumed on success. */
static bencode_value_t *decode_body(const unsigned char *buf, size_t len, size_t *consumed) {
    int64_t body = nornd_ipc_frame_len(buf, len);
    if (body < 0 || body > NORND_IPC_MAX_BODY) return NULL;
    if ((size_t)4 + (size_t)body > len) return NULL; /* incomplete */
    size_t pos = 0;
    bencode_value_t *d = bencode_decode((const char *)buf + 4, (size_t)body, &pos);
    if (!d) return NULL;
    if (d->type != BENCODE_DICT) {
        bencode_free(d);
        return NULL;
    }
    *consumed = (size_t)4 + (size_t)body;
    return d;
}

int nornd_ipc_decode_req(const unsigned char *buf, size_t len,
                         nornd_ipc_req_t *r, size_t *consumed) {
    if (!buf || !r) return -1;
    size_t used = 0;
    bencode_value_t *d = decode_body(buf, len, &used);
    if (!d) return -1;
    memset(r, 0, sizeof(*r));
    int ok = 1;

    bencode_value_t *op = bencode_dict_get(d, "op");
    if (!op || op->type != BENCODE_STRING || op->val.str_val.len == 0 ||
        op->val.str_val.len >= NORND_IPC_MAX_OP) {
        ok = 0;
    } else {
        memcpy(r->op, op->val.str_val.data, op->val.str_val.len);
        r->op[op->val.str_val.len] = '\0';
    }
    if (ok && get_str(d, "key", r->key, sizeof(r->key), &r->klen, NULL) != 0) ok = 0;
    if (ok && get_str(d, "val", r->val, sizeof(r->val), &r->vlen, &r->has_val) != 0) ok = 0;
    if (ok && get_str(d, "expect", r->expect, sizeof(r->expect), &r->elen, &r->has_expect) != 0)
        ok = 0;
    if (ok) {
        bencode_value_t *idv = bencode_dict_get(d, "id");
        if (idv) {
            if (idv->type != BENCODE_STRING || idv->val.str_val.len != NORND_IPC_ID_BYTES)
                ok = 0;
            else {
                memcpy(r->id, idv->val.str_val.data, NORND_IPC_ID_BYTES);
                r->has_id = 1;
            }
        }
    }
    if (ok) {
        bencode_value_t *sv = bencode_dict_get(d, "seq");
        if (sv) {
            if (sv->type != BENCODE_INT)
                ok = 0;
            else {
                r->seq = sv->val.int_val;
                r->has_seq = 1;
            }
        }
    }
    bencode_free(d);
    if (!ok) return -1;
    if (consumed) *consumed = used;
    return 0;
}

int nornd_ipc_decode_resp(const unsigned char *buf, size_t len,
                          nornd_ipc_resp_t *r, size_t *consumed) {
    if (!buf || !r) return -1;
    size_t used = 0;
    bencode_value_t *d = decode_body(buf, len, &used);
    if (!d) return -1;
    memset(r, 0, sizeof(*r));
    int ok = 1;

    bencode_value_t *okv = bencode_dict_get(d, "ok");
    if (!okv || okv->type != BENCODE_INT)
        ok = 0;
    else
        r->ok = okv->val.int_val ? 1 : 0;

    if (ok && get_str(d, "val", r->val, sizeof(r->val), &r->vlen, &r->has_val) != 0) ok = 0;
    if (ok) {
        size_t el = 0;
        if (get_str(d, "err", (unsigned char *)r->err, sizeof(r->err) - 1, &el, &r->has_err) != 0)
            ok = 0;
        else if (r->has_err)
            r->err[el] = '\0';
    }
    if (ok) {
        bencode_value_t *items = bencode_dict_get(d, "items");
        if (items) {
            if (items->type != BENCODE_LIST || items->val.list_val.count > NORND_IPC_MAX_ITEMS) {
                ok = 0;
            } else {
                for (size_t i = 0; i < items->val.list_val.count && ok; i++) {
                    bencode_value_t *it = items->val.list_val.items[i];
                    if (it->type != BENCODE_STRING || it->val.str_val.len > NORND_IPC_MAX_ITEM) {
                        ok = 0;
                    } else {
                        memcpy(r->items[i].data, it->val.str_val.data, it->val.str_val.len);
                        r->items[i].len = it->val.str_val.len;
                        r->n_items++;
                    }
                }
            }
        }
    }
    bencode_free(d);
    if (!ok) return -1;
    if (consumed) *consumed = used;
    return 0;
}
