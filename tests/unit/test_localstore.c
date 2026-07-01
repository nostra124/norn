/* SPDX-License-Identifier: MIT */
/* Unit tests for the local served-KV store (FEAT-node-set). 100% cov. */
#include "localstore.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int visit_count;
static void visit_fn(void *ud, const unsigned char *key, size_t klen,
                     const unsigned char *val, size_t vlen) {
    (void)ud; (void)key; (void)klen; (void)val; (void)vlen;
    visit_count++;
}

static void test_put_get(void) {
    localstore_t s;
    localstore_init(&s);
    unsigned char out[64];
    /* missing key */
    assert(localstore_get(&s, (const unsigned char *)"nope", 4, out, sizeof(out)) == -1);
    /* put + get */
    assert(localstore_put(&s, (const unsigned char *)"k", 1, (const unsigned char *)"v", 1) == 0);
    int n = localstore_get(&s, (const unsigned char *)"k", 1, out, sizeof(out));
    assert(n == 1 && out[0] == 'v');
    /* replace */
    assert(localstore_put(&s, (const unsigned char *)"k", 1, (const unsigned char *)"vv", 2) == 0);
    n = localstore_get(&s, (const unsigned char *)"k", 1, out, sizeof(out));
    assert(n == 2 && memcmp(out, "vv", 2) == 0);
    printf("  test_put_get: OK\n");
}

static void test_put_errors(void) {
    localstore_t s;
    localstore_init(&s);
    /* NULL / empty / oversized */
    assert(localstore_put(NULL, (const unsigned char *)"k", 1, (const unsigned char *)"v", 1) == -1);
    assert(localstore_put(&s, NULL, 1, (const unsigned char *)"v", 1) == -1);
    assert(localstore_put(&s, (const unsigned char *)"k", 0, (const unsigned char *)"v", 1) == -1);
    assert(localstore_put(&s, (const unsigned char *)"k", 1, NULL, 1) == -1);
    unsigned char bigkey[LOCALSTORE_MAX_KEY + 1];
    memset(bigkey, 'x', sizeof(bigkey));
    assert(localstore_put(&s, bigkey, sizeof(bigkey), (const unsigned char *)"v", 1) == -1);
    unsigned char bigval[LOCALSTORE_MAX_VAL + 1];
    memset(bigval, 'y', sizeof(bigval));
    assert(localstore_put(&s, (const unsigned char *)"k", 1, bigval, sizeof(bigval)) == -1);
    printf("  test_put_errors: OK\n");
}

static void test_get_errors(void) {
    localstore_t s;
    localstore_init(&s);
    unsigned char out[8];
    assert(localstore_get(NULL, (const unsigned char *)"k", 1, out, 1) == -1);
    assert(localstore_get(&s, NULL, 1, out, 1) == -1);
    assert(localstore_get(&s, (const unsigned char *)"k", 0, out, 1) == -1);
    assert(localstore_get(&s, (const unsigned char *)"k", 1, NULL, 1) == -1);
    /* value larger than cap */
    assert(localstore_put(&s, (const unsigned char *)"k", 1, (const unsigned char *)"vvvv", 4) == 0);
    assert(localstore_get(&s, (const unsigned char *)"k", 1, out, 2) == -1);
    printf("  test_get_errors: OK\n");
}

static void test_list(void) {
    localstore_t s;
    localstore_init(&s);
    /* empty list */
    visit_count = 0;
    assert(localstore_list(&s, (const unsigned char *)"", 0, visit_fn, NULL) == 0);
    assert(visit_count == 0);
    /* add three keys, list all */
    assert(localstore_put(&s, (const unsigned char *)"a", 1, (const unsigned char *)"1", 1) == 0);
    assert(localstore_put(&s, (const unsigned char *)"ab", 2, (const unsigned char *)"2", 1) == 0);
    assert(localstore_put(&s, (const unsigned char *)"b", 1, (const unsigned char *)"3", 1) == 0);
    visit_count = 0;
    assert(localstore_list(&s, (const unsigned char *)"", 0, visit_fn, NULL) == 3);
    assert(visit_count == 3);
    /* prefix "a" matches "a" and "ab" */
    visit_count = 0;
    assert(localstore_list(&s, (const unsigned char *)"a", 1, visit_fn, NULL) == 2);
    assert(visit_count == 2);
    /* NULL args */
    assert(localstore_list(NULL, (const unsigned char *)"", 0, visit_fn, NULL) == -1);
    assert(localstore_list(&s, (const unsigned char *)"", 0, NULL, NULL) == -1);
    printf("  test_list: OK\n");
}

static void test_capacity(void) {
    localstore_t s;
    localstore_init(&s);
    /* fill to the cap */
    char key[16];
    for (int i = 0; i < LOCALSTORE_MAX_ENTRIES; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        assert(localstore_put(&s, (const unsigned char *)key, strlen(key),
                              (const unsigned char *)"v", 1) == 0);
    }
    /* one more overflows */
    snprintf(key, sizeof(key), "k%d", LOCALSTORE_MAX_ENTRIES);
    assert(localstore_put(&s, (const unsigned char *)key, strlen(key),
                          (const unsigned char *)"v", 1) == -1);
    /* but replacing an existing key still works */
    assert(localstore_put(&s, (const unsigned char *)"k0", 2,
                          (const unsigned char *)"x", 1) == 0);
    printf("  test_capacity: OK\n");
}

int main(void) {
    printf("test_localstore:\n");
    test_put_get();
    test_put_errors();
    test_get_errors();
    test_list();
    test_capacity();
    printf("test_localstore: OK\n");
    return 0;
}