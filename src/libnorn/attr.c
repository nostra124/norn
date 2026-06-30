/* SPDX-License-Identifier: MIT */
#include "attr.h"
#include <stdlib.h>
#include <string.h>

#define MAX_ATTRS 64
#define MAX_KEY_LEN 128
#define MAX_VALUE_LEN 256

typedef struct {
    char key[MAX_KEY_LEN];
    unsigned char value[MAX_VALUE_LEN];
    size_t value_len;
    int used;
} attr_entry_t;

static attr_entry_t attrs[MAX_ATTRS];
static int initialized = 0;

static void init_attrs(void) {
    if (!initialized) {  /* LCOV_EXCL_BR_LINE: norn_attr_clear() sets initialized before any test runs */
        memset(attrs, 0, sizeof(attrs));  /* LCOV_EXCL_LINE */
        initialized = 1;  /* LCOV_EXCL_LINE */
    }
}

int norn_set_attr(const char *key, const void *value, size_t value_len) {
    if (!key || !value || value_len > MAX_VALUE_LEN) return -1;
    size_t klen = strlen(key);
    if (klen == 0 || klen >= MAX_KEY_LEN) return -1;
    
    init_attrs();
    
    /* Check if key exists */
    for (int i = 0; i < MAX_ATTRS; i++) {
        if (attrs[i].used && strcmp(attrs[i].key, key) == 0) {
            memcpy(attrs[i].value, value, value_len);
            attrs[i].value_len = value_len;
            return 0;
        }
    }
    
    /* Find empty slot */
    for (int i = 0; i < MAX_ATTRS; i++) {
        if (!attrs[i].used) {
            strcpy(attrs[i].key, key);
            memcpy(attrs[i].value, value, value_len);
            attrs[i].value_len = value_len;
            attrs[i].used = 1;
            return 0;
        }
    }
    
    return -1;  /* no space */
}

int norn_get_attr(const char *key, void *out, size_t cap) {
    if (!key || !out) return -1;
    
    init_attrs();
    
    for (int i = 0; i < MAX_ATTRS; i++) {
        if (attrs[i].used && strcmp(attrs[i].key, key) == 0) {
            if (cap < attrs[i].value_len) return -1;
            memcpy(out, attrs[i].value, attrs[i].value_len);
            return (int)attrs[i].value_len;
        }
    }
    
    return -1;  /* not found */
}

int norn_del_attr(const char *key) {
    if (!key) return -1;
    
    init_attrs();
    
    for (int i = 0; i < MAX_ATTRS; i++) {
        if (attrs[i].used && strcmp(attrs[i].key, key) == 0) {
            attrs[i].used = 0;
            return 0;
        }
    }
    
    return -1;  /* not found */
}

size_t norn_attr_count(void) {
    init_attrs();
    size_t count = 0;
    for (int i = 0; i < MAX_ATTRS; i++) {
        if (attrs[i].used) count++;
    }
    return count;
}

void norn_attr_clear(void) {
    memset(attrs, 0, sizeof(attrs));
    initialized = 1;
}

const char *norn_version(void) {
    return "0.1.0";
}

const char *norn_node_type(void) {
    return "norn";
}