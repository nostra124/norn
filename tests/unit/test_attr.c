/* SPDX-License-Identifier: MIT */
/* Test node attributes API. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "attr.h"

static void test_basic_set_get(void) {
    norn_attr_clear();
    
    const char *key = "version";
    const char *value = "0.20.1";
    int rc = norn_set_attr(key, value, strlen(value));
    assert(rc == 0);
    
    char buf[64];
    int len = norn_get_attr(key, buf, sizeof(buf));
    assert(len == (int)strlen(value));
    assert(memcmp(buf, value, len) == 0);
    
    norn_attr_clear();
}

static void test_replace(void) {
    norn_attr_clear();
    
    norn_set_attr("key", "value1", 6);
    char buf[64];
    int len = norn_get_attr("key", buf, sizeof(buf));
    assert(len == 6);
    assert(memcmp(buf, "value1", 6) == 0);
    
    norn_set_attr("key", "value2", 6);
    len = norn_get_attr("key", buf, sizeof(buf));
    assert(len == 6);
    assert(memcmp(buf, "value2", 6) == 0);
    
    norn_attr_clear();
}

static void test_delete(void) {
    norn_attr_clear();
    
    norn_set_attr("key", "value", 5);
    assert(norn_del_attr("key") == 0);
    
    char buf[64];
    assert(norn_get_attr("key", buf, sizeof(buf)) == -1);
    
    norn_attr_clear();
}

static void test_not_found(void) {
    norn_attr_clear();
    
    char buf[64];
    assert(norn_get_attr("nonexistent", buf, sizeof(buf)) == -1);
    assert(norn_del_attr("nonexistent") == -1);
    
    norn_attr_clear();
}

static void test_count(void) {
    norn_attr_clear();
    
    assert(norn_attr_count() == 0);
    
    norn_set_attr("key1", "v1", 2);
    assert(norn_attr_count() == 1);
    
    norn_set_attr("key2", "v2", 2);
    assert(norn_attr_count() == 2);
    
    norn_del_attr("key1");
    assert(norn_attr_count() == 1);
    
    norn_attr_clear();
    assert(norn_attr_count() == 0);
}

static void test_buffer_too_small(void) {
    norn_attr_clear();
    
    const char *value = "this is a long value";
    norn_set_attr("key", value, strlen(value));
    
    char buf[5];
    int len = norn_get_attr("key", buf, sizeof(buf));
    assert(len == -1);  /* buffer too small */
    
    char bigbuf[64];
    len = norn_get_attr("key", bigbuf, sizeof(bigbuf));
    assert(len == (int)strlen(value));
    
    norn_attr_clear();
}

static void test_null_params(void) {
    norn_attr_clear();
    
    assert(norn_set_attr(NULL, "v", 1) == -1);
    assert(norn_set_attr("key", NULL, 1) == -1);
    assert(norn_get_attr(NULL, NULL, 0) == -1);
    assert(norn_del_attr(NULL) == -1);
    
    norn_attr_clear();
}

static void test_empty_key(void) {
    norn_attr_clear();

    assert(norn_set_attr("", "value", 5) == -1);

    norn_attr_clear();
}

static void test_invalid_args(void) {
    norn_attr_clear();

    /* value_len exceeding the max is rejected (attr.c:27). */
    char big[300];
    memset(big, 'x', sizeof(big));
    assert(norn_set_attr("k", big, 257) == -1);

    /* Key at/over the max length is rejected (attr.c:29). */
    char longkey[200];
    memset(longkey, 'a', sizeof(longkey) - 1);
    longkey[sizeof(longkey) - 1] = '\0';
    assert(norn_set_attr(longkey, "v", 1) == -1);

    /* get with a non-NULL key but NULL out is rejected (attr.c:57). */
    assert(norn_get_attr("k", NULL, 0) == -1);

    norn_attr_clear();
}

static void test_lookup_skips_other_used(void) {
    /* With used entries present whose keys don't match, get/del must skip them
     * and report not-found (attr.c:62 and attr.c:78 used-but-no-match arms). */
    norn_attr_clear();

    norn_set_attr("alpha", "1", 1);
    norn_set_attr("beta", "2", 1);

    char buf[16];
    assert(norn_get_attr("gamma", buf, sizeof(buf)) == -1);
    assert(norn_del_attr("gamma") == -1);

    norn_attr_clear();
}

static void test_binary_value(void) {
    norn_attr_clear();
    
    unsigned char binary[256];
    for (int i = 0; i < 256; i++) binary[i] = (unsigned char)i;
    
    int rc = norn_set_attr("binary", binary, sizeof(binary));
    assert(rc == 0);
    
    unsigned char buf[256];
    int len = norn_get_attr("binary", buf, sizeof(buf));
    assert(len == 256);
    assert(memcmp(buf, binary, 256) == 0);
    
    norn_attr_clear();
}

static void test_builtin_attrs(void) {
    const char *ver = norn_version();
    assert(ver != NULL);
    assert(strlen(ver) > 0);
    
    const char *type = norn_node_type();
    assert(type != NULL);
    assert(strcmp(type, "norn") == 0);
}

static void test_lazy_init(void) {
    /* This test is called after test_first_access_without_clear,
     * so initialized is already 1. We just verify set/get still works. */
    norn_attr_clear();
    
    int rc = norn_set_attr("test", "value", 5);
    assert(rc == 0);
    
    norn_attr_clear();
}

static void test_max_attrs(void) {
    norn_attr_clear();
    
    char key[32];
    char value[32];
    
    /* Fill up to MAX_ATTRS */
    for (int i = 0; i < 64; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "val%d", i);
        int rc = norn_set_attr(key, value, strlen(value));
        assert(rc == 0);
    }
    
    assert(norn_attr_count() == 64);
    
    /* 65th should fail */
    int rc = norn_set_attr("key65", "value", 5);
    assert(rc == -1);
    
    norn_attr_clear();
}

int main(void) {
    test_basic_set_get();
    test_replace();
    test_delete();
    test_not_found();
    test_count();
    test_buffer_too_small();
    test_null_params();
    test_empty_key();
    test_invalid_args();
    test_lookup_skips_other_used();
    test_binary_value();
    test_builtin_attrs();
    test_lazy_init();
    test_max_attrs();
    
    printf("test_attr: OK\n");
    return 0;
}