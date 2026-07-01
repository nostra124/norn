/* SPDX-License-Identifier: MIT */
/**
 * @file publog.h
 * @brief Local publish log for `norn bep44 set/put` (FEAT-bep44-list).
 *
 * Tracks the records this node has published itself (mutable + immutable), so
 * `norn bep44 list` can show them alongside the records this node is holding
 * for the DHT. In-memory, bounded; not persisted across restarts.
 */
#ifndef NORND_PUBLOG_H
#define NORND_PUBLOG_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define PUBLOG_MAX 256

typedef struct {
    unsigned char target[20];  /* DHT key the record was published under */
    int immutable;             /* 1 = immutable, 0 = mutable */
    char name[128];            /* the salt/name (mutable; "" for immutable) */
    size_t vlen;               /* value length */
    uint32_t seq;              /* sequence number (mutable only) */
    time_t published;          /* when published */
    int used;
} publog_entry_t;

typedef struct {
    publog_entry_t entries[PUBLOG_MAX];
} publog_t;

void publog_init(publog_t *p);

/* Record a publish. Returns 0 on success, -1 on overflow/bad args. */
int publog_add(publog_t *p, const unsigned char *target, int immutable,
               const char *name, size_t vlen, uint32_t seq);

/* Count entries. */
int publog_count(const publog_t *p);

/* Get entry i (0-based); returns NULL if out of range. */
const publog_entry_t *publog_get(const publog_t *p, int i);

#endif /* NORND_PUBLOG_H */