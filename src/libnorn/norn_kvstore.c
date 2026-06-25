/**
 * @file norn_kvstore.c
 * @brief Replicated KV state machine (FEAT-026). See norn_kvstore.h.
 *
 * Pure and deterministic: the same command sequence yields the same map on
 * every replica. Bounded static-ish memory (a fixed entry table). Exercised to
 * full line/branch coverage in isolation.
 */

#include "norn_kvstore.h"

#include <stdlib.h>
#include <string.h>

#define KV_MAX_WATCH 16

typedef struct {
    unsigned char key[NORN_KV_MAX_KEY];
    size_t klen;
    unsigned char val[NORN_KV_MAX_VAL];
    size_t vlen;
    int used;
} kv_entry_t;

typedef struct {
    unsigned char prefix[NORN_KV_MAX_KEY];
    size_t plen;
    norn_kv_watch_fn fn;
    void *ud;
} kv_watch_t;

struct norn_kv {
    kv_entry_t *entries;
    int cap;
    int count;
    kv_watch_t watches[KV_MAX_WATCH];
    int n_watches;
};

/* ---- helpers ---- */

static kv_entry_t *find(const norn_kv_t *kv, const unsigned char *key, size_t klen) {
    for (int i = 0; i < kv->cap; i++) {
        kv_entry_t *e = &kv->entries[i];
        if (e->used && e->klen == klen && memcmp(e->key, key, klen) == 0) return e;
    }
    return NULL;
}

static kv_entry_t *alloc_slot(norn_kv_t *kv) {
    for (int i = 0; i < kv->cap; i++)
        if (!kv->entries[i].used) return &kv->entries[i];
    return NULL;
}

static void fire(norn_kv_t *kv, norn_kv_event_t ev, const unsigned char *key,
                 size_t klen, const unsigned char *val, size_t vlen) {
    for (int i = 0; i < kv->n_watches; i++) {
        kv_watch_t *w = &kv->watches[i];
        if (w->plen <= klen && memcmp(w->prefix, key, w->plen) == 0)
            w->fn(w->ud, ev, key, klen, val, vlen);
    }
}

/* Insert or overwrite key→val. Returns 1 on success, -1 on overflow. */
static int store_put(norn_kv_t *kv, const unsigned char *key, size_t klen,
                     const unsigned char *val, size_t vlen) {
    kv_entry_t *e = find(kv, key, klen);
    if (!e) {
        e = alloc_slot(kv);
        if (!e) return -1;
        memcpy(e->key, key, klen);
        e->klen = klen;
        e->used = 1;
        kv->count++;
    }
    memcpy(e->val, val, vlen);
    e->vlen = vlen;
    fire(kv, NORN_KV_EV_PUT, key, klen, val, vlen);
    return 1;
}

static int store_del(norn_kv_t *kv, const unsigned char *key, size_t klen) {
    kv_entry_t *e = find(kv, key, klen);
    if (!e) return 0;
    e->used = 0;
    kv->count--;
    fire(kv, NORN_KV_EV_DEL, key, klen, NULL, 0);
    return 1;
}

/* ---- encoding ---- */

static int put_u16(unsigned char *p, size_t v) {
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)(v & 0xff);
    return 2;
}
static size_t get_u16(const unsigned char *p) {
    return ((size_t)p[0] << 8) | p[1];
}

int norn_kv_encode_put(unsigned char *buf, size_t cap,
                       const unsigned char *key, size_t klen,
                       const unsigned char *val, size_t vlen) {
    if (!buf || !key || klen == 0 || klen > NORN_KV_MAX_KEY || vlen > NORN_KV_MAX_VAL)
        return -1;
    if (vlen && !val) return -1;
    size_t need = 1 + 2 + klen + 2 + vlen;
    if (cap < need) return -1;
    size_t o = 0;
    buf[o++] = NORN_KV_PUT;
    o += put_u16(buf + o, klen);
    memcpy(buf + o, key, klen);
    o += klen;
    o += put_u16(buf + o, vlen);
    if (vlen) memcpy(buf + o, val, vlen);
    o += vlen;
    return (int)o;
}

int norn_kv_encode_del(unsigned char *buf, size_t cap,
                       const unsigned char *key, size_t klen) {
    if (!buf || !key || klen == 0 || klen > NORN_KV_MAX_KEY) return -1;
    size_t need = 1 + 2 + klen;
    if (cap < need) return -1;
    size_t o = 0;
    buf[o++] = NORN_KV_DEL;
    o += put_u16(buf + o, klen);
    memcpy(buf + o, key, klen);
    o += klen;
    return (int)o;
}

int norn_kv_encode_cas(unsigned char *buf, size_t cap,
                       const unsigned char *key, size_t klen,
                       const unsigned char *expect, size_t elen,
                       const unsigned char *val, size_t vlen) {
    if (!buf || !key || klen == 0 || klen > NORN_KV_MAX_KEY) return -1;
    if (elen > NORN_KV_MAX_VAL || vlen > NORN_KV_MAX_VAL) return -1;
    if ((elen && !expect) || (vlen && !val)) return -1;
    size_t need = 1 + 2 + klen + 2 + elen + 2 + vlen;
    if (cap < need) return -1;
    size_t o = 0;
    buf[o++] = NORN_KV_CAS;
    o += put_u16(buf + o, klen);
    memcpy(buf + o, key, klen);
    o += klen;
    o += put_u16(buf + o, elen);
    if (elen) memcpy(buf + o, expect, elen);
    o += elen;
    o += put_u16(buf + o, vlen);
    if (vlen) memcpy(buf + o, val, vlen);
    o += vlen;
    return (int)o;
}

/* ---- apply ---- */

int norn_kv_apply(norn_kv_t *kv, const unsigned char *cmd, size_t len) {
    if (!kv || !cmd || len < 3) return -1;
    size_t o = 0;
    unsigned char op = cmd[o++];
    size_t klen = get_u16(cmd + o);
    o += 2;
    if (klen == 0 || klen > NORN_KV_MAX_KEY || o + klen > len) return -1;
    const unsigned char *key = cmd + o;
    o += klen;

    switch (op) {
    case NORN_KV_PUT: {
        if (o + 2 > len) return -1;
        size_t vlen = get_u16(cmd + o);
        o += 2;
        if (vlen > NORN_KV_MAX_VAL || o + vlen != len) return -1;
        return store_put(kv, key, klen, cmd + o, vlen);
    }
    case NORN_KV_DEL:
        if (o != len) return -1;
        return store_del(kv, key, klen);
    case NORN_KV_CAS: {
        if (o + 2 > len) return -1;
        size_t elen = get_u16(cmd + o);
        o += 2;
        if (elen > NORN_KV_MAX_VAL || o + elen + 2 > len) return -1;
        const unsigned char *expect = cmd + o;
        o += elen;
        size_t vlen = get_u16(cmd + o);
        o += 2;
        if (vlen > NORN_KV_MAX_VAL || o + vlen != len) return -1;
        const unsigned char *val = cmd + o;
        /* Current value (absent key == empty value). */
        kv_entry_t *e = find(kv, key, klen);
        size_t cur_len = e ? e->vlen : 0;
        const unsigned char *cur = e ? e->val : NULL;
        if (cur_len != elen || (elen && memcmp(cur, expect, elen) != 0))
            return 0; /* mismatch → no-op */
        return store_put(kv, key, klen, val, vlen);
    }
    default:
        return -1;
    }
}

/* ---- reads ---- */

int norn_kv_get(const norn_kv_t *kv, const unsigned char *key, size_t klen,
                unsigned char *out, size_t cap) {
    if (!kv || !key || klen == 0) return -1;
    kv_entry_t *e = find(kv, key, klen);
    if (!e) return -1;
    if (e->vlen > cap) return -1;
    if (e->vlen && out) memcpy(out, e->val, e->vlen);
    return (int)e->vlen;
}

int norn_kv_count(const norn_kv_t *kv) { return kv ? kv->count : -1; }

/* ---- watches ---- */

int norn_kv_watch(norn_kv_t *kv, const unsigned char *prefix, size_t plen,
                  norn_kv_watch_fn fn, void *ud) {
    if (!kv || !fn || plen > NORN_KV_MAX_KEY) return -1;
    if (plen && !prefix) return -1;
    if (kv->n_watches >= KV_MAX_WATCH) return -1;
    kv_watch_t *w = &kv->watches[kv->n_watches++];
    if (plen) memcpy(w->prefix, prefix, plen);
    w->plen = plen;
    w->fn = fn;
    w->ud = ud;
    return 0;
}

/* ---- snapshots ---- */

int norn_kv_snapshot(const norn_kv_t *kv, unsigned char *buf, size_t cap) {
    if (!kv || !buf) return -1;
    size_t o = 0;
    for (int i = 0; i < kv->cap; i++) {
        kv_entry_t *e = &kv->entries[i];
        if (!e->used) continue;
        size_t need = 2 + e->klen + 2 + e->vlen;
        if (o + need > cap) return -1;
        o += put_u16(buf + o, e->klen);
        memcpy(buf + o, e->key, e->klen);
        o += e->klen;
        o += put_u16(buf + o, e->vlen);
        memcpy(buf + o, e->val, e->vlen);
        o += e->vlen;
    }
    return (int)o;
}

int norn_kv_restore(norn_kv_t *kv, const unsigned char *buf, size_t len) {
    if (!kv || (!buf && len)) return -1;
    /* Clear. */
    for (int i = 0; i < kv->cap; i++) kv->entries[i].used = 0;
    kv->count = 0;

    size_t o = 0;
    while (o < len) {
        if (o + 2 > len) return -1;
        size_t klen = get_u16(buf + o);
        o += 2;
        if (klen == 0 || klen > NORN_KV_MAX_KEY || o + klen + 2 > len) return -1;
        const unsigned char *key = buf + o;
        o += klen;
        size_t vlen = get_u16(buf + o);
        o += 2;
        if (vlen > NORN_KV_MAX_VAL || o + vlen > len) return -1;
        kv_entry_t *e = alloc_slot(kv);
        if (!e) return -1;
        memcpy(e->key, key, klen);
        e->klen = klen;
        memcpy(e->val, buf + o, vlen);
        e->vlen = vlen;
        e->used = 1;
        kv->count++;
        o += vlen;
    }
    return 0;
}

/* ---- lifecycle ---- */

norn_kv_t *norn_kv_new(int max_entries) {
    if (max_entries <= 0) return NULL;
    norn_kv_t *kv = calloc(1, sizeof(*kv));
    if (!kv) return NULL; /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
    kv->entries = calloc((size_t)max_entries, sizeof(kv_entry_t));
    if (!kv->entries) { /* LCOV_EXCL_BR_LINE: malloc failure not unit-tested */
        free(kv);       /* LCOV_EXCL_LINE */
        return NULL;    /* LCOV_EXCL_LINE */
    }
    kv->cap = max_entries;
    return kv;
}

void norn_kv_free(norn_kv_t *kv) {
    if (!kv) return;
    free(kv->entries);
    free(kv);
}
