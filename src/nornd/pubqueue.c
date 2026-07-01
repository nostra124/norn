/* SPDX-License-Identifier: MIT */
#include "pubqueue.h"

#include <string.h>

void pubqueue_init(pubqueue_t *q) {
    if (q) memset(q, 0, sizeof(*q));
}

int pubqueue_add_mutable(pubqueue_t *q, const unsigned char *pubkey,
                         const unsigned char *secret, const unsigned char *value,
                         size_t vlen, uint32_t seq, const char *name) {
    if (!q || !pubkey || !secret || !value || vlen > PUBQUEUE_MAX_VAL) return -1;
    for (int i = 0; i < PUBQUEUE_MAX; i++) {
        if (q->entries[i].used) continue;
        memcpy(q->entries[i].pubkey, pubkey, 32);
        memcpy(q->entries[i].secret, secret, 64);
        memcpy(q->entries[i].value, value, vlen);
        q->entries[i].vlen = vlen;
        q->entries[i].seq = seq;
        q->entries[i].immutable = 0;
        q->entries[i].used = 1;
        if (name) {
            strncpy(q->entries[i].name, name, sizeof(q->entries[i].name) - 1);
            q->entries[i].name[sizeof(q->entries[i].name) - 1] = '\0';
        }
        return 0;
    }
    return -1;
}

int pubqueue_add_immutable(pubqueue_t *q, const unsigned char *value, size_t vlen) {
    if (!q || !value || vlen > PUBQUEUE_MAX_VAL) return -1;
    for (int i = 0; i < PUBQUEUE_MAX; i++) {
        if (q->entries[i].used) continue;
        memcpy(q->entries[i].value, value, vlen);
        q->entries[i].vlen = vlen;
        q->entries[i].immutable = 1;
        q->entries[i].used = 1;
        return 0;
    }
    return -1;
}

int pubqueue_count(const pubqueue_t *q) {
    if (!q) return 0;
    int n = 0;
    for (int i = 0; i < PUBQUEUE_MAX; i++) if (q->entries[i].used) n++;
    return n;
}

const pubqueue_entry_t *pubqueue_get(const pubqueue_t *q, int i) {
    if (!q || i < 0) return NULL;
    int idx = 0;
    for (int j = 0; j < PUBQUEUE_MAX; j++) {
        if (!q->entries[j].used) continue;
        if (idx == i) return &q->entries[j];
        idx++;
    }
    return NULL;
}

void pubqueue_done(pubqueue_t *q, int i) {
    if (!q || i < 0) return;
    int idx = 0;
    for (int j = 0; j < PUBQUEUE_MAX; j++) {
        if (!q->entries[j].used) continue;
        if (idx == i) { q->entries[j].used = 0; return; }
        idx++;
    }
}