/* SPDX-License-Identifier: MIT */
#ifndef NORND_PUBQUEUE_H
#define NORND_PUBQUEUE_H

#include <stddef.h>
#include <stdint.h>

#define PUBQUEUE_MAX 64
#define PUBQUEUE_MAX_VAL 1000

typedef struct {
    unsigned char target[20];
    unsigned char pubkey[32];
    unsigned char secret[64];
    unsigned char value[PUBQUEUE_MAX_VAL];
    size_t vlen;
    uint32_t seq;
    char name[128];
    int immutable;
    int used;
} pubqueue_entry_t;

typedef struct {
    pubqueue_entry_t entries[PUBQUEUE_MAX];
} pubqueue_t;

void pubqueue_init(pubqueue_t *q);

/* Enqueue a mutable publish. Returns 0 / -1. */
int pubqueue_add_mutable(pubqueue_t *q, const unsigned char *pubkey,
                         const unsigned char *secret, const unsigned char *value,
                         size_t vlen, uint32_t seq, const char *name);

/* Enqueue an immutable publish. Returns 0 / -1. */
int pubqueue_add_immutable(pubqueue_t *q, const unsigned char *value, size_t vlen);

/* Count pending entries. */
int pubqueue_count(const pubqueue_t *q);

/* Get entry i; NULL if out of range. */
const pubqueue_entry_t *pubqueue_get(const pubqueue_t *q, int i);

/* Mark entry i as done (published). */
void pubqueue_done(pubqueue_t *q, int i);

#endif /* NORND_PUBQUEUE_H */