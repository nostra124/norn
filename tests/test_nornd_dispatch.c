/* Unit tests for the nornd IPC dispatcher (FEAT-029). Pure → 100% coverage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "dispatch.h"

/* ---- fake cluster backend, fully controllable from the tests ---- */
static int g_leader_present;
static int g_is_leader;
static int g_put_rc;
static int g_del_rc;
static int g_members_n;
static unsigned char g_leader_pk[NORND_PUBKEY];

static int g_have;
static unsigned char g_key[256];
static size_t g_klen;
static unsigned char g_val[4096];
static size_t g_vlen;

static int fk_put(void *c, const unsigned char *k, size_t kl,
                  const unsigned char *v, size_t vl) {
    (void)c;
    if (g_put_rc == 0) {
        memcpy(g_key, k, kl);
        g_klen = kl;
        memcpy(g_val, v, vl);
        g_vlen = vl;
        g_have = 1;
    }
    return g_put_rc;
}
static int fk_del(void *c, const unsigned char *k, size_t kl) {
    (void)c;
    (void)k;
    (void)kl;
    if (g_del_rc == 0) g_have = 0;
    return g_del_rc;
}
static int fk_get(void *c, const unsigned char *k, size_t kl, unsigned char *out,
                  size_t cap) {
    (void)c;
    (void)cap;
    if (g_have && kl == g_klen && memcmp(k, g_key, kl) == 0) {
        memcpy(out, g_val, g_vlen);
        return (int)g_vlen;
    }
    return -1;
}
static int fk_is_leader(void *c) {
    (void)c;
    return g_is_leader;
}
static const unsigned char *fk_leader(void *c) {
    (void)c;
    return g_leader_present ? g_leader_pk : NULL;
}
static int fk_members(void *c, unsigned char out[][NORND_PUBKEY], int max) {
    (void)c;
    if (g_members_n < 0) return -1;
    int n = g_members_n < max ? g_members_n : max;
    for (int i = 0; i < n; i++) memset(out[i], 0x10 + i, NORND_PUBKEY);
    return n;
}

static nornd_backend_t BE = {NULL,     fk_put,       fk_del,    fk_get,
                             fk_is_leader, fk_leader, fk_members};

static nornd_ipc_req_t mkreq(const char *op) {
    nornd_ipc_req_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.op, op);
    return r;
}
static void setkey(nornd_ipc_req_t *r, const char *k) {
    r->klen = strlen(k);
    memcpy(r->key, k, r->klen);
}
static void setval(nornd_ipc_req_t *r, const char *v) {
    r->has_val = 1;
    r->vlen = strlen(v);
    memcpy(r->val, v, r->vlen);
}
static void setexpect(nornd_ipc_req_t *r, const char *e) {
    r->has_expect = 1;
    r->elen = strlen(e);
    memcpy(r->expect, e, r->elen);
}

static void reset_backend(void) {
    g_leader_present = g_is_leader = 0;
    g_put_rc = g_del_rc = 0;
    g_members_n = 0;
    g_have = 0;
    g_klen = g_vlen = 0;
    memset(g_leader_pk, 0xAB, sizeof(g_leader_pk));
}

static void test_get(void) {
    reset_backend();
    nornd_ipc_resp_t r;
    /* missing key */
    nornd_ipc_req_t q = mkreq("get");
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && r.has_err);
    /* absent */
    setkey(&q, "k1");
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "not found") == 0);
    /* present */
    g_have = 1;
    g_klen = 2;
    memcpy(g_key, "k1", 2);
    memcpy(g_val, "hello", 5);
    g_vlen = 5;
    nornd_dispatch(&BE, &q, &r);
    assert(r.ok && r.has_val && r.vlen == 5 && memcmp(r.val, "hello", 5) == 0);
}

static void test_put_del(void) {
    reset_backend();
    nornd_ipc_resp_t r;
    /* put missing key */
    nornd_ipc_req_t q = mkreq("put");
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "missing key") == 0);
    /* put missing val */
    setkey(&q, "k");
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "missing val") == 0);
    /* put accepted */
    setval(&q, "v");
    g_put_rc = 0;
    nornd_dispatch(&BE, &q, &r);
    assert(r.ok && g_have);
    /* put no leader */
    g_put_rc = -1;
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "no leader") == 0);

    /* del missing key */
    nornd_ipc_req_t d = mkreq("del");
    nornd_dispatch(&BE, &d, &r);
    assert(!r.ok && strcmp(r.err, "missing key") == 0);
    /* del accepted */
    setkey(&d, "k");
    g_del_rc = 0;
    nornd_dispatch(&BE, &d, &r);
    assert(r.ok && !g_have);
    /* del no leader */
    g_del_rc = -1;
    nornd_dispatch(&BE, &d, &r);
    assert(!r.ok && strcmp(r.err, "no leader") == 0);
}

static void test_cas(void) {
    reset_backend();
    nornd_ipc_resp_t r;
    nornd_ipc_req_t q = mkreq("cas");
    /* missing key */
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "missing key") == 0);
    setkey(&q, "k");
    /* has_expect false (short-circuit) */
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "cas needs expect and val") == 0);
    /* has_expect true but has_val false */
    setexpect(&q, "old");
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "cas needs expect and val") == 0);
    setval(&q, "new");
    /* key absent */
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "cas: key absent") == 0);
    /* present but length differs (n != elen short-circuit) */
    g_have = 1;
    g_klen = 1;
    memcpy(g_key, "k", 1);
    memcpy(g_val, "ol", 2); /* len 2 != elen 3 */
    g_vlen = 2;
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "cas: mismatch") == 0);
    /* present, same length but different bytes (memcmp != 0) */
    memcpy(g_val, "OLD", 3);
    g_vlen = 3;
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "cas: mismatch") == 0);
    /* match → put accepted */
    memcpy(g_val, "old", 3);
    g_vlen = 3;
    g_put_rc = 0;
    nornd_dispatch(&BE, &q, &r);
    assert(r.ok);
    /* match → put no leader */
    memcpy(g_val, "old", 3);
    g_vlen = 3;
    g_have = 1;
    g_put_rc = -1;
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "no leader") == 0);
}

static void test_members(void) {
    reset_backend();
    nornd_ipc_resp_t r;
    nornd_ipc_req_t q = mkreq("members");
    /* three members */
    g_members_n = 3;
    nornd_dispatch(&BE, &q, &r);
    assert(r.ok && r.n_items == 3);
    assert(r.items[0].len == NORND_PUBKEY && r.items[0].data[0] == 0x10);
    /* backend returns -1 → clamped to zero items */
    g_members_n = -1;
    nornd_dispatch(&BE, &q, &r);
    assert(r.ok && r.n_items == 0);
}

static void test_leader(void) {
    reset_backend();
    nornd_ipc_resp_t r;
    nornd_ipc_req_t q = mkreq("leader");
    /* unknown */
    nornd_dispatch(&BE, &q, &r);
    assert(!r.ok && strcmp(r.err, "no leader") == 0);
    /* known */
    g_leader_present = 1;
    nornd_dispatch(&BE, &q, &r);
    assert(r.ok && r.has_val && r.vlen == NORND_PUBKEY &&
           r.val[0] == 0xAB);
}

static void test_status(void) {
    reset_backend();
    nornd_ipc_resp_t r;
    nornd_ipc_req_t q = mkreq("status");
    /* follower, no leader, no members */
    nornd_dispatch(&BE, &q, &r);
    assert(r.ok && r.has_val && r.vlen == 1 && r.val[0] == 0);
    assert(r.n_items == 0);
    /* leader known + self is leader + members */
    g_leader_present = 1;
    g_is_leader = 1;
    g_members_n = 2;
    nornd_dispatch(&BE, &q, &r);
    assert(r.ok && r.vlen == 1 + NORND_PUBKEY && r.val[0] == 1);
    assert(r.val[1] == 0xAB && r.n_items == 2);
}

static void test_watch_and_unknown(void) {
    reset_backend();
    nornd_ipc_resp_t r;
    nornd_ipc_req_t w = mkreq("watch");
    nornd_dispatch(&BE, &w, &r);
    assert(r.ok);
    nornd_ipc_req_t u = mkreq("bogus");
    nornd_dispatch(&BE, &u, &r);
    assert(!r.ok && strcmp(r.err, "unknown op") == 0);
}

int main(void) {
    test_get();
    test_put_del();
    test_cas();
    test_members();
    test_leader();
    test_status();
    test_watch_and_unknown();
    printf("all nornd dispatch tests passed\n");
    return 0;
}
