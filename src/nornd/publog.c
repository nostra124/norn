/* SPDX-License-Identifier: MIT */
#include "publog.h"

#include <string.h>

void publog_init(publog_t *p) {
    if (p) memset(p, 0, sizeof(*p));
}

int publog_add(publog_t *p, const unsigned char *target, int immutable,
               const char *name, size_t vlen, uint32_t seq) {
    if (!p || !target) return -1;
    /* Replace if the same target already logged. */
    for (int i = 0; i < PUBLOG_MAX; i++) {
        if (!p->entries[i].used) continue;
        if (memcmp(p->entries[i].target, target, 20) == 0) {
            p->entries[i].immutable = immutable;
            p->entries[i].vlen = vlen;
            p->entries[i].seq = seq;
            p->entries[i].published = time(NULL);
            if (name) {
                strncpy(p->entries[i].name, name, sizeof(p->entries[i].name) - 1);
                p->entries[i].name[sizeof(p->entries[i].name) - 1] = '\0';
            } else {
                p->entries[i].name[0] = '\0';
            }
            return 0;
        }
    }
    for (int i = 0; i < PUBLOG_MAX; i++) {
        if (p->entries[i].used) continue;
        memcpy(p->entries[i].target, target, 20);
        p->entries[i].immutable = immutable;
        p->entries[i].vlen = vlen;
        p->entries[i].seq = seq;
        p->entries[i].published = time(NULL);
        p->entries[i].used = 1;
        if (name) {
            strncpy(p->entries[i].name, name, sizeof(p->entries[i].name) - 1);
            p->entries[i].name[sizeof(p->entries[i].name) - 1] = '\0';
        } else {
            p->entries[i].name[0] = '\0';
        }
        return 0;
    }
    return -1;
}

int publog_count(const publog_t *p) {
    if (!p) return 0;
    int n = 0;
    for (int i = 0; i < PUBLOG_MAX; i++) if (p->entries[i].used) n++;
    return n;
}

const publog_entry_t *publog_get(const publog_t *p, int i) {
    if (!p || i < 0) return NULL;
    int idx = 0;
    for (int j = 0; j < PUBLOG_MAX; j++) {
        if (!p->entries[j].used) continue;
        if (idx == i) return &p->entries[j];
        idx++;
    }
    return NULL;
}