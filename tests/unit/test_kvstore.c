/* SPDX-License-Identifier: MIT */
/* Unit tests for the replicated KV state machine (FEAT-026), norn_kvstore.
 * Pure module — exercised directly to 100% line + branch coverage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "norn_kvstore.h"

#define U(s) ((const unsigned char *)(s))

/* Apply an encoded command, asserting the encode succeeded. */
static int put(norn_kv_t *kv, const char *k, const char *v) {
    unsigned char buf[512];
    int n = norn_kv_encode_put(buf, sizeof(buf), U(k), strlen(k), U(v), strlen(v));
    assert(n > 0);
    return norn_kv_apply(kv, buf, (size_t)n);
}
static int del(norn_kv_t *kv, const char *k) {
    unsigned char buf[128];
    int n = norn_kv_encode_del(buf, sizeof(buf), U(k), strlen(k));
    assert(n > 0);
    return norn_kv_apply(kv, buf, (size_t)n);
}
static int cas(norn_kv_t *kv, const char *k, const char *exp, const char *v) {
    unsigned char buf[768];
    int n = norn_kv_encode_cas(buf, sizeof(buf), U(k), strlen(k),
                               U(exp), strlen(exp), U(v), strlen(v));
    assert(n > 0);
    return norn_kv_apply(kv, buf, (size_t)n);
}
static int has(norn_kv_t *kv, const char *k, const char *v) {
    unsigned char out[512];
    int n = norn_kv_get(kv, U(k), strlen(k), out, sizeof(out));
    return n == (int)strlen(v) && memcmp(out, v, n) == 0;
}

static void test_put_get_del(void) {
    norn_kv_t *kv = norn_kv_new(8);
    assert(kv);
    assert(norn_kv_count(kv) == 0);
    assert(put(kv, "a", "1") == 1);
    assert(put(kv, "b", "two") == 1);
    assert(norn_kv_count(kv) == 2);
    assert(has(kv, "a", "1") && has(kv, "b", "two"));
    /* overwrite */
    assert(put(kv, "a", "11") == 1);
    assert(has(kv, "a", "11") && norn_kv_count(kv) == 2);
    /* delete */
    assert(del(kv, "a") == 1);
    assert(norn_kv_count(kv) == 1);
    assert(norn_kv_get(kv, U("a"), 1, NULL, 0) == -1);
    /* delete absent → no-op */
    assert(del(kv, "zzz") == 0);
    /* empty value */
    assert(put(kv, "e", "") == 1);
    unsigned char tmp[4];
    assert(norn_kv_get(kv, U("e"), 1, tmp, sizeof(tmp)) == 0);
    norn_kv_free(kv);
}

static void test_cas(void) {
    norn_kv_t *kv = norn_kv_new(8);
    /* CAS create: absent key matches empty expect. */
    assert(cas(kv, "k", "", "v0") == 1);
    assert(has(kv, "k", "v0"));
    /* CAS with wrong expected → no-op. */
    assert(cas(kv, "k", "WRONG", "v1") == 0);
    assert(has(kv, "k", "v0"));
    /* CAS with right expected → set. */
    assert(cas(kv, "k", "v0", "v1") == 1);
    assert(has(kv, "k", "v1"));
    /* CAS create when key already exists (empty expect, non-empty current) → no-op. */
    assert(cas(kv, "k", "", "v2") == 0);
    assert(has(kv, "k", "v1"));
    norn_kv_free(kv);
}

static int g_ev_count;
static norn_kv_event_t g_last_ev;
static unsigned char g_last_key[64];
static size_t g_last_klen;
static void watch_cb(void *ud, norn_kv_event_t ev, const unsigned char *key, size_t klen,
                     const unsigned char *val, size_t vlen) {
    (void)val;
    (void)vlen;
    int *counter = ud;
    if (counter) (*counter)++;
    g_ev_count++;
    g_last_ev = ev;
    g_last_klen = klen;
    memcpy(g_last_key, key, klen);
}

static void test_watches(void) {
    norn_kv_t *kv = norn_kv_new(16);
    int user_count = 0;
    /* prefix "us/" watch + a catch-all watch (empty prefix). */
    assert(norn_kv_watch(kv, U("us/"), 3, watch_cb, &user_count) == 0);
    assert(norn_kv_watch(kv, U(""), 0, watch_cb, NULL) == 0);

    g_ev_count = 0;
    put(kv, "us/alice", "x"); /* matches both watches → 2 events */
    assert(g_ev_count == 2 && user_count == 1);
    assert(g_last_ev == NORN_KV_EV_PUT);

    g_ev_count = 0;
    put(kv, "other", "y"); /* only the catch-all matches → 1 event */
    assert(g_ev_count == 1 && user_count == 1);

    g_ev_count = 0;
    del(kv, "us/alice"); /* both watches, DEL event */
    assert(g_ev_count == 2 && g_last_ev == NORN_KV_EV_DEL);

    /* fill the watch table to capacity then overflow. */
    for (int i = 0; i < 16; i++) {
        int rc = norn_kv_watch(kv, U("p"), 1, watch_cb, NULL);
        if (rc != 0) break;
    }
    assert(norn_kv_watch(kv, U("p"), 1, watch_cb, NULL) == -1);
    norn_kv_free(kv);
}

static void test_snapshot_restore(void) {
    norn_kv_t *kv = norn_kv_new(8);
    put(kv, "a", "1");
    put(kv, "b", "22");
    put(kv, "c", "333");
    unsigned char snap[1024];
    int n = norn_kv_snapshot(kv, snap, sizeof(snap));
    assert(n > 0);

    norn_kv_t *kv2 = norn_kv_new(8);
    put(kv2, "stale", "X"); /* will be cleared by restore */
    assert(norn_kv_restore(kv2, snap, (size_t)n) == 0);
    assert(norn_kv_count(kv2) == 3);
    assert(has(kv2, "a", "1") && has(kv2, "b", "22") && has(kv2, "c", "333"));
    assert(norn_kv_get(kv2, U("stale"), 5, NULL, 0) == -1);

    /* snapshot into a too-small buffer → -1. */
    unsigned char tiny[4];
    assert(norn_kv_snapshot(kv, tiny, sizeof(tiny)) == -1);

    /* empty store snapshot/restore round-trips to empty. */
    norn_kv_t *empty = norn_kv_new(4);
    int en = norn_kv_snapshot(empty, snap, sizeof(snap));
    assert(en == 0);
    assert(norn_kv_restore(kv2, snap, 0) == 0 && norn_kv_count(kv2) == 0);
    norn_kv_free(empty);
    norn_kv_free(kv2);
    norn_kv_free(kv);
}

static void test_capacity_and_errors(void) {
    norn_kv_t *kv = norn_kv_new(2);
    assert(put(kv, "a", "1") == 1);
    assert(put(kv, "b", "2") == 1);
    /* third distinct key → overflow. */
    assert(put(kv, "c", "3") == -1);
    /* overwriting an existing key still works at capacity. */
    assert(put(kv, "a", "9") == 1);
    norn_kv_free(kv);

    /* encode arg validation. */
    unsigned char buf[600];
    assert(norn_kv_encode_put(NULL, 10, U("k"), 1, U("v"), 1) == -1);
    assert(norn_kv_encode_put(buf, sizeof(buf), NULL, 1, U("v"), 1) == -1);
    assert(norn_kv_encode_put(buf, sizeof(buf), U("k"), 0, U("v"), 1) == -1);
    unsigned char bigk[NORN_KV_MAX_KEY + 1] = {0};
    assert(norn_kv_encode_put(buf, sizeof(buf), bigk, sizeof(bigk), U("v"), 1) == -1);
    assert(norn_kv_encode_put(buf, sizeof(buf), U("k"), 1, NULL, 5) == -1); /* vlen>0,val NULL */
    assert(norn_kv_encode_put(buf, 3, U("key"), 3, U("val"), 3) == -1);     /* cap too small */
    assert(norn_kv_encode_del(buf, 2, U("key"), 3) == -1);
    assert(norn_kv_encode_del(NULL, 10, U("k"), 1) == -1);
    assert(norn_kv_encode_cas(buf, sizeof(buf), U("k"), 1, NULL, 5, U("v"), 1) == -1);
    assert(norn_kv_encode_cas(buf, 4, U("k"), 1, U(""), 0, U("v"), 1) == -1);
    unsigned char bigv[NORN_KV_MAX_VAL + 1] = {0};
    assert(norn_kv_encode_cas(buf, sizeof(buf), U("k"), 1, bigv, sizeof(bigv), U("v"), 1) == -1);
}

static void test_apply_malformed(void) {
    norn_kv_t *kv = norn_kv_new(4);
    unsigned char b[64];
    assert(norn_kv_apply(NULL, b, 5) == -1);
    assert(norn_kv_apply(kv, NULL, 5) == -1);
    assert(norn_kv_apply(kv, b, 2) == -1); /* too short */
    /* unknown opcode */
    b[0] = 99; b[1] = 0; b[2] = 1; b[3] = 'k';
    assert(norn_kv_apply(kv, b, 4) == -1);
    /* PUT with klen overrunning the buffer */
    b[0] = NORN_KV_PUT; b[1] = 0; b[2] = 50;
    assert(norn_kv_apply(kv, b, 4) == -1);
    /* klen zero */
    b[0] = NORN_KV_PUT; b[1] = 0; b[2] = 0;
    assert(norn_kv_apply(kv, b, 5) == -1);
    /* PUT missing value-length field */
    b[0] = NORN_KV_PUT; b[1] = 0; b[2] = 1; b[3] = 'k';
    assert(norn_kv_apply(kv, b, 4) == -1);
    /* PUT with trailing garbage (vlen != remaining) */
    b[0] = NORN_KV_PUT; b[1] = 0; b[2] = 1; b[3] = 'k'; b[4] = 0; b[5] = 1; b[6] = 'v'; b[7] = 'X';
    assert(norn_kv_apply(kv, b, 8) == -1);
    /* DEL with trailing garbage */
    b[0] = NORN_KV_DEL; b[1] = 0; b[2] = 1; b[3] = 'k'; b[4] = 'X';
    assert(norn_kv_apply(kv, b, 5) == -1);
    /* CAS truncated (missing expect length) */
    b[0] = NORN_KV_CAS; b[1] = 0; b[2] = 1; b[3] = 'k';
    assert(norn_kv_apply(kv, b, 4) == -1);
    /* CAS expect length overruns */
    b[0] = NORN_KV_CAS; b[1] = 0; b[2] = 1; b[3] = 'k'; b[4] = 0; b[5] = 40;
    assert(norn_kv_apply(kv, b, 6) == -1);
    /* CAS value-length field missing after expect */
    b[0] = NORN_KV_CAS; b[1] = 0; b[2] = 1; b[3] = 'k'; b[4] = 0; b[5] = 0;
    assert(norn_kv_apply(kv, b, 6) == -1);
    /* CAS value overruns */
    b[0] = NORN_KV_CAS; b[1] = 0; b[2] = 1; b[3] = 'k'; b[4] = 0; b[5] = 0; b[6] = 0; b[7] = 9;
    assert(norn_kv_apply(kv, b, 8) == -1);

    /* restore malformed */
    assert(norn_kv_restore(kv, b, 1) == -1);          /* dangling length */
    unsigned char bad[3] = {0, 0, 0};                 /* klen 0 */
    assert(norn_kv_restore(kv, bad, 3) == -1);
    norn_kv_free(kv);
}

static void test_null_and_lifecycle(void) {
    assert(norn_kv_new(0) == NULL);
    assert(norn_kv_new(-1) == NULL);
    assert(norn_kv_count(NULL) == -1);
    assert(norn_kv_get(NULL, U("k"), 1, NULL, 0) == -1);
    assert(norn_kv_watch(NULL, U("p"), 1, watch_cb, NULL) == -1);
    norn_kv_t *kv = norn_kv_new(2);
    assert(norn_kv_get(kv, NULL, 1, NULL, 0) == -1);
    assert(norn_kv_get(kv, U("k"), 0, NULL, 0) == -1);
    assert(norn_kv_watch(kv, NULL, 1, watch_cb, NULL) == -1); /* plen>0, prefix NULL */
    assert(norn_kv_watch(kv, U("p"), 1, NULL, NULL) == -1);   /* fn NULL */
    unsigned char bigp[NORN_KV_MAX_KEY + 1] = {0};
    assert(norn_kv_watch(kv, bigp, sizeof(bigp), watch_cb, NULL) == -1);
    /* get with value larger than caller buffer → -1 */
    put(kv, "k", "value");
    unsigned char small[2];
    assert(norn_kv_get(kv, U("k"), 1, small, sizeof(small)) == -1);
    assert(norn_kv_snapshot(NULL, small, 2) == -1);
    assert(norn_kv_restore(NULL, small, 2) == -1);
    assert(norn_kv_restore(kv, NULL, 2) == -1);
    norn_kv_free(NULL);
    norn_kv_free(kv);
}

static void test_branch_corners(void) {
    unsigned char buf[600];
    /* encode_put vlen > MAX */
    unsigned char bigv[NORN_KV_MAX_VAL + 1] = {0};
    assert(norn_kv_encode_put(buf, sizeof(buf), U("k"), 1, bigv, sizeof(bigv)) == -1);
    /* encode_del each operand */
    assert(norn_kv_encode_del(buf, sizeof(buf), NULL, 1) == -1);
    assert(norn_kv_encode_del(buf, sizeof(buf), U("k"), 0) == -1);
    unsigned char bigk[NORN_KV_MAX_KEY + 1] = {0};
    assert(norn_kv_encode_del(buf, sizeof(buf), bigk, sizeof(bigk)) == -1);
    /* encode_cas operands */
    assert(norn_kv_encode_cas(NULL, 10, U("k"), 1, U(""), 0, U("v"), 1) == -1);
    assert(norn_kv_encode_cas(buf, sizeof(buf), NULL, 1, U(""), 0, U("v"), 1) == -1);
    assert(norn_kv_encode_cas(buf, sizeof(buf), U("k"), 0, U(""), 0, U("v"), 1) == -1);
    assert(norn_kv_encode_cas(buf, sizeof(buf), bigk, sizeof(bigk), U(""), 0, U("v"), 1) == -1);
    assert(norn_kv_encode_cas(buf, sizeof(buf), U("k"), 1, U(""), 0, bigv, sizeof(bigv)) == -1);
    assert(norn_kv_encode_cas(buf, sizeof(buf), U("k"), 1, U("e"), 1, NULL, 5) == -1);
    /* encode_cas with empty new value (vlen==0 branch) */
    int n = norn_kv_encode_cas(buf, sizeof(buf), U("k"), 1, U(""), 0, U(""), 0);
    assert(n > 0);
    norn_kv_t *kv = norn_kv_new(4);
    assert(norn_kv_apply(kv, buf, (size_t)n) == 1); /* creates empty-valued key */

    /* watch: a key shorter than a prefix (plen<=klen false). */
    int c = 0;
    norn_kv_watch(kv, U("longpfx"), 7, watch_cb, &c);
    put(kv, "x", "1");
    assert(c == 0); /* "x" shorter than prefix → not matched */

    /* get with NULL out but a present non-empty value → returns length, no copy. */
    put(kv, "kk", "hello");
    assert(norn_kv_get(kv, U("kk"), 2, NULL, 100) == 5);

    /* CAS same-length but different content → mismatch (memcmp branch). */
    assert(cas(kv, "kk", "world", "z") == 0);
    assert(has(kv, "kk", "hello"));

    /* apply: klen > MAX. */
    unsigned char b[8] = {NORN_KV_PUT, 0, NORN_KV_MAX_KEY + 1, 0, 0, 0, 0, 0};
    assert(norn_kv_apply(kv, b, 8) == -1);
    /* apply PUT vlen > MAX. */
    unsigned char pb[300];
    pb[0] = NORN_KV_PUT; pb[1] = 0; pb[2] = 1; pb[3] = 'k';
    pb[4] = (unsigned char)((NORN_KV_MAX_VAL + 1) >> 8); pb[5] = (unsigned char)((NORN_KV_MAX_VAL + 1) & 0xff);
    assert(norn_kv_apply(kv, pb, 6) == -1);
    /* apply CAS elen > MAX. */
    unsigned char cb[8] = {NORN_KV_CAS, 0, 1, 'k',
                           (unsigned char)((NORN_KV_MAX_VAL + 1) >> 8),
                           (unsigned char)((NORN_KV_MAX_VAL + 1) & 0xff), 0, 0};
    assert(norn_kv_apply(kv, cb, 8) == -1);
    /* apply CAS vlen > MAX (elen 0, then huge vlen). */
    unsigned char cb2[10] = {NORN_KV_CAS, 0, 1, 'k', 0, 0,
                             (unsigned char)((NORN_KV_MAX_VAL + 1) >> 8),
                             (unsigned char)((NORN_KV_MAX_VAL + 1) & 0xff), 0, 0};
    assert(norn_kv_apply(kv, cb2, 10) == -1);

    /* snapshot NULL buf; restore NULL buf len 0 (the (!buf && len) false path). */
    assert(norn_kv_snapshot(kv, NULL, 10) == -1);
    assert(norn_kv_restore(kv, NULL, 0) == 0);

    /* restore: klen > MAX, vlen > MAX, and over-capacity. */
    unsigned char bk[4] = {0, NORN_KV_MAX_KEY + 1, 0, 0};
    assert(norn_kv_restore(kv, bk, 4) == -1);
    unsigned char bv[7] = {0, 1, 'k', (unsigned char)((NORN_KV_MAX_VAL + 1) >> 8),
                           (unsigned char)((NORN_KV_MAX_VAL + 1) & 0xff), 0, 0};
    assert(norn_kv_restore(kv, bv, 5) == -1);
    /* restore: valid klen but the value-length field is truncated. */
    unsigned char trunc_kl[4] = {0, 2, 'a', 'b'};
    assert(norn_kv_restore(kv, trunc_kl, 4) == -1);
    /* restore: valid vlen field but the value bytes run past the buffer. */
    unsigned char trunc_vl[5] = {0, 1, 'k', 0, 5};
    assert(norn_kv_restore(kv, trunc_vl, 5) == -1);

    /* over-capacity restore: a 3-entry snapshot into a cap-2 store. */
    norn_kv_t *src = norn_kv_new(4);
    put(src, "a", "1"); put(src, "b", "2"); put(src, "c", "3");
    unsigned char snap[512];
    int sn = norn_kv_snapshot(src, snap, sizeof(snap));
    assert(sn > 0);
    norn_kv_t *small = norn_kv_new(2);
    assert(norn_kv_restore(small, snap, (size_t)sn) == -1);
    norn_kv_free(small);
    norn_kv_free(src);
    norn_kv_free(kv);
}

struct visited {
    int n;
};
static void count_visit(void *ud, const unsigned char *key, size_t klen,
                        const unsigned char *val, size_t vlen) {
    (void)key;
    (void)klen;
    (void)val;
    (void)vlen;
    ((struct visited *)ud)->n++;
}

static void test_foreach(void) {
    norn_kv_t *kv = norn_kv_new(8); /* leaves unused slots → exercises the skip */
    assert(put(kv, "a", "1") == 1);
    assert(put(kv, "ab", "2") == 1);
    assert(put(kv, "b", "3") == 1);

    struct visited v;
    v.n = 0;
    assert(norn_kv_foreach(kv, NULL, 0, count_visit, &v) == 3); /* empty prefix → all */
    assert(v.n == 3);
    v.n = 0;
    assert(norn_kv_foreach(kv, U("a"), 1, count_visit, &v) == 2); /* a, ab; "b" mismatch skipped */
    assert(v.n == 2);
    v.n = 0;
    assert(norn_kv_foreach(kv, U("ab"), 2, count_visit, &v) == 1); /* ab; "a"/"b" klen<plen skipped */
    assert(v.n == 1);
    v.n = 0;
    assert(norn_kv_foreach(kv, U("z"), 1, count_visit, &v) == 0); /* no match */
    assert(v.n == 0);

    /* bad args */
    unsigned char bigp[NORN_KV_MAX_KEY + 1];
    memset(bigp, 'x', sizeof(bigp));
    assert(norn_kv_foreach(NULL, U("a"), 1, count_visit, &v) == -1);
    assert(norn_kv_foreach(kv, U("a"), 1, NULL, &v) == -1);
    assert(norn_kv_foreach(kv, bigp, sizeof(bigp), count_visit, &v) == -1);
    assert(norn_kv_foreach(kv, NULL, 1, count_visit, &v) == -1); /* plen && !prefix */
    norn_kv_free(kv);
}

int main(void) {
    test_put_get_del();
    test_foreach();
    test_cas();
    test_watches();
    test_snapshot_restore();
    test_capacity_and_errors();
    test_apply_malformed();
    test_null_and_lifecycle();
    test_branch_corners();
    printf("test_kvstore: all passed\n");
    return 0;
}
