/* Unit tests for the node-served KV request handler (FEAT-033). 100% cov. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "served.h"

/* ---- fake mutable backend (cluster KV stand-in) ---- */
static int g_get_present;        /* GET returns a value when set            */
static int g_list_rc;            /* LIST return code (count, or -1)          */
static int g_list_emit;          /* how many keys LIST emits                 */
static int g_list_bigkey;        /* emit one over-long key to fill the inbuf */

static int fk_get(void *c, const unsigned char *k, size_t kl, unsigned char *o,
                  size_t cap) {
    (void)c;
    (void)k;
    (void)kl;
    if (!g_get_present) return -1;
    const char *v = "value-bytes";
    size_t n = strlen(v);
    assert(n <= cap);
    memcpy(o, v, n);
    return (int)n;
}

static int fk_list(void *c, const unsigned char *prefix, size_t plen,
                   norn_kv_visit_fn fn, void *ud) {
    (void)c;
    (void)prefix;
    (void)plen;
    if (g_list_rc < 0) return -1;
    if (g_list_bigkey) {
        /* A key larger than the inline buffer forces the drop branch. */
        static unsigned char big[NORND_IPC_MAX_VAL + 16];
        memset(big, 'K', sizeof(big));
        fn(ud, big, sizeof(big), NULL, 0);
        return 1;
    }
    for (int i = 0; i < g_list_emit; i++) {
        char key[16];
        int kn = snprintf(key, sizeof(key), "k%d", i);
        fn(ud, (const unsigned char *)key, (size_t)kn, NULL, 0);
    }
    return g_list_emit;
}

static nornd_served_req_t mkreq(nornd_served_verb_t v, const char *arg) {
    nornd_served_req_t r;
    memset(&r, 0, sizeof(r));
    r.verb = v;
    if (arg) {
        r.arglen = strlen(arg);
        memcpy(r.arg, arg, r.arglen);
    }
    return r;
}

int main(void) {
    char tmp[256];
    strcpy(tmp, "/tmp/norn_served_testXXXXXX");
    assert(mkdtemp(tmp) != NULL);
    nornd_store_t store;
    assert(nornd_store_init(&store, tmp) == 0);
    char hash[NORND_STORE_HASH_HEX + 1];
    const unsigned char obj[] = "immutable blob";
    assert(nornd_store_put(&store, obj, sizeof(obj) - 1, hash) == 0);

    nornd_served_backend_t be = {NULL, fk_get, fk_list, &store};
    nornd_served_result_t res;

    /* GET hit. */
    g_get_present = 1;
    nornd_served_req_t q = mkreq(NORND_SERVED_GET, "k");
    nornd_served_handle(&be, &q, &res);
    assert(res.ok && !res.stream_file && res.len == strlen("value-bytes"));
    assert(res.inlen == res.len && memcmp(res.inbuf, "value-bytes", res.inlen) == 0);

    /* GET miss. */
    g_get_present = 0;
    nornd_served_handle(&be, &q, &res);
    assert(!res.ok && strcmp(res.err, "not found") == 0);

    /* CAT hit → streams the backing file. */
    nornd_served_req_t c = mkreq(NORND_SERVED_CAT, hash);
    nornd_served_handle(&be, &c, &res);
    assert(res.ok && res.stream_file && res.len == sizeof(obj) - 1);
    assert(strstr(res.path, hash) != NULL);

    /* CAT miss (well-formed but absent hash). */
    char absent[NORND_STORE_HASH_HEX + 1];
    memset(absent, '0', NORND_STORE_HASH_HEX);
    absent[NORND_STORE_HASH_HEX] = '\0';
    nornd_served_req_t cm = mkreq(NORND_SERVED_CAT, absent);
    nornd_served_handle(&be, &cm, &res);
    assert(!res.ok && strcmp(res.err, "not found") == 0);

    /* LIST success: two keys joined by newlines. */
    g_list_rc = 0;
    g_list_emit = 2;
    g_list_bigkey = 0;
    nornd_served_req_t l = mkreq(NORND_SERVED_LIST, "k");
    nornd_served_handle(&be, &l, &res);
    assert(res.ok && !res.stream_file);
    res.inbuf[res.inlen] = '\0';
    assert(strcmp((char *)res.inbuf, "k0\nk1\n") == 0 && res.len == res.inlen);

    /* LIST whose first key overflows the inline buffer is dropped. */
    g_list_bigkey = 1;
    nornd_served_handle(&be, &l, &res);
    assert(res.ok && res.inlen == 0 && res.len == 0);

    /* LIST backend failure. */
    g_list_bigkey = 0;
    g_list_rc = -1;
    nornd_served_handle(&be, &l, &res);
    assert(!res.ok && strcmp(res.err, "list failed") == 0);

    printf("all nornd served tests passed\n");
    return 0;
}
