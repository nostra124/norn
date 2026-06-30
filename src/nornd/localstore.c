/* SPDX-License-Identifier: MIT */
/**
 * @file localstore.c
 * @brief Local served-KV store for `norn node set`. See localstore.h.
 */

#include "localstore.h"

#include <string.h>

void localstore_init(localstore_t *s) {
    if (s) memset(s, 0, sizeof(*s));
}

int localstore_put(localstore_t *s, const unsigned char *key, size_t klen,
                   const unsigned char *val, size_t vlen) {
    if (!s || !key || klen == 0 || klen > LOCALSTORE_MAX_KEY ||
        !val || vlen > LOCALSTORE_MAX_VAL)
        return -1;
    /* Replace an existing entry. */
    for (int i = 0; i < LOCALSTORE_MAX_ENTRIES; i++) {
        if (!s->entries[i].used) continue;
        if (s->entries[i].klen == klen &&
            memcmp(s->entries[i].key, key, klen) == 0) {
            memcpy(s->entries[i].val, val, vlen);
            s->entries[i].vlen = vlen;
            return 0;
        }
    }
    /* Find a free slot. */
    for (int i = 0; i < LOCALSTORE_MAX_ENTRIES; i++) {
        if (s->entries[i].used) continue;
        memcpy(s->entries[i].key, key, klen);
        s->entries[i].klen = klen;
        memcpy(s->entries[i].val, val, vlen);
        s->entries[i].vlen = vlen;
        s->entries[i].used = 1;
        return 0;
    }
    return -1; /* table full */
}

int localstore_get(const localstore_t *s, const unsigned char *key, size_t klen,
                   unsigned char *out, size_t cap) {
    if (!s || !key || klen == 0 || !out) return -1;
    for (int i = 0; i < LOCALSTORE_MAX_ENTRIES; i++) {
        if (!s->entries[i].used) continue;
        if (s->entries[i].klen == klen &&
            memcmp(s->entries[i].key, key, klen) == 0) {
            if (s->entries[i].vlen > cap) return -1;
            memcpy(out, s->entries[i].val, s->entries[i].vlen);
            return (int)s->entries[i].vlen;
        }
    }
    return -1;
}

int localstore_list(const localstore_t *s, const unsigned char *prefix,
                    size_t plen, norn_kv_visit_fn fn, void *ud) {
    if (!s || !fn) return -1;
    int n = 0;
    for (int i = 0; i < LOCALSTORE_MAX_ENTRIES; i++) {
        if (!s->entries[i].used) continue;
        if (plen > 0 && s->entries[i].klen >= plen &&
            memcmp(s->entries[i].key, prefix, plen) != 0)
            continue;
        if (plen > 0 && s->entries[i].klen < plen) continue;
        fn(ud, s->entries[i].key, s->entries[i].klen,
           s->entries[i].val, s->entries[i].vlen);
        n++;
    }
    return n;
}