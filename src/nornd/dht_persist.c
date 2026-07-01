/* SPDX-License-Identifier: MIT */
/**
 * @file dht_persist.c
 * @brief Persist the DHT store + publog across nornd restarts (nornd-only).
 *
 * Format (binary, versioned):
 *   magic(4)  version(4)
 *   publog_count(4)
 *   for each publog entry:
 *     target(20) immutable(1) vlen(4) seq(4) published(8) name_len(1) name(<=127) value(vlen)
 *   dht_count(4)
 *   for each held record:
 *     target(20) immutable(1) vlen(4) seq(4) pubkey(32) sig(64) value(vlen)
 */
#include "dht_persist.h"
#include "publog.h"

#include "norn.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define DHT_PERSIST_MAGIC 0x4e525044u  /* "NRPD" */
#define DHT_PERSIST_VERSION 1u

int dht_persist_save(const char *path, publog_t *publog) {
    if (!path) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t magic = DHT_PERSIST_MAGIC, ver = DHT_PERSIST_VERSION;
    fwrite(&magic, 4, 1, f);
    fwrite(&ver, 4, 1, f);

    /* Publog */
    uint32_t pn = publog ? publog_count(publog) : 0;
    fwrite(&pn, 4, 1, f);
    for (uint32_t i = 0; i < pn; i++) {
        const publog_entry_t *e = publog_get(publog, (int)i);
        if (!e) continue;
        fwrite(e->target, 20, 1, f);
        uint8_t imm = e->immutable ? 1 : 0;
        fwrite(&imm, 1, 1, f);
        uint32_t vl = (uint32_t)e->vlen;
        fwrite(&vl, 4, 1, f);
        fwrite(&e->seq, 4, 1, f);
        fwrite(&e->published, sizeof(time_t), 1, f);
        uint8_t nl = (uint8_t)strlen(e->name);
        fwrite(&nl, 1, 1, f);
        if (nl) fwrite(e->name, nl, 1, f);
        if (e->vlen) fwrite(e->value, e->vlen, 1, f);
    }

    /* Held DHT records */
    uint32_t total = 0;
    for (int kind = 0; kind < 2; kind++) {
        norn_dht_item_t items[128];
        int n = norn_dht_list(kind, items, 128);
        if (n > 0) total += (uint32_t)n;
    }
    fwrite(&total, 4, 1, f);
    for (int kind = 0; kind < 2; kind++) {
        norn_dht_item_t items[128];
        int n = norn_dht_list(kind, items, 128);
        if (n < 0) n = 0;
        for (int i = 0; i < n; i++) {
            unsigned char pk[32], sig[64], vbuf[1024];
            uint32_t seq;
            size_t vlen;
            int imm;
            int got = norn_dht_get_full(items[i].target, pk, &seq, vbuf, sizeof(vbuf),
                                        &vlen, sig, &imm);
            if (got != 1) continue;
            fwrite(items[i].target, 20, 1, f);
            uint8_t im = imm ? 1 : 0;
            fwrite(&im, 1, 1, f);
            uint32_t vl = (uint32_t)vlen;
            fwrite(&vl, 4, 1, f);
            fwrite(&seq, 4, 1, f);
            fwrite(pk, 32, 1, f);
            fwrite(sig, 64, 1, f);
            if (vlen) fwrite(vbuf, vlen, 1, f);
        }
    }
    fclose(f);
    return 0;
}

int dht_persist_load(const char *path, publog_t *publog) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t magic = 0, ver = 0;
    if (fread(&magic, 4, 1, f) != 1 || fread(&ver, 4, 1, f) != 1 ||
        magic != DHT_PERSIST_MAGIC || ver != DHT_PERSIST_VERSION) {
        fclose(f);
        return -1;
    }
    /* Publog */
    uint32_t pn = 0;
    if (fread(&pn, 4, 1, f) != 1) { fclose(f); return -1; }
    for (uint32_t i = 0; i < pn; i++) {
        unsigned char target[20];
        uint8_t imm;
        uint32_t vl, seq;
        time_t published;
        uint8_t nl;
        if (fread(target, 20, 1, f) != 1 || fread(&imm, 1, 1, f) != 1 ||
            fread(&vl, 4, 1, f) != 1 || fread(&seq, 4, 1, f) != 1 ||
            fread(&published, sizeof(time_t), 1, f) != 1 || fread(&nl, 1, 1, f) != 1) {
            fclose(f); return -1;
        }
        char name[128] = {0};
        if (nl && fread(name, nl, 1, f) != 1) { fclose(f); return -1; }
        unsigned char value[PUBLOG_MAX_VAL];
        if (vl && vl <= sizeof(value) && fread(value, vl, 1, f) != 1) { fclose(f); return -1; }
        if (publog)
            publog_add(publog, target, imm ? 1 : 0, nl ? name : NULL,
                       vl ? value : NULL, vl, seq);
    }
    /* Held DHT records */
    uint32_t total = 0;
    if (fread(&total, 4, 1, f) != 1) { fclose(f); return -1; }
    for (uint32_t i = 0; i < total; i++) {
        unsigned char target[20], pk[32], sig[64], vbuf[1024];
        uint8_t imm;
        uint32_t vl, seq;
        if (fread(target, 20, 1, f) != 1 || fread(&imm, 1, 1, f) != 1 ||
            fread(&vl, 4, 1, f) != 1 || fread(&seq, 4, 1, f) != 1 ||
            fread(pk, 32, 1, f) != 1 || fread(sig, 64, 1, f) != 1) {
            fclose(f); return -1;
        }
        if (vl && vl <= sizeof(vbuf) && fread(vbuf, vl, 1, f) != 1) { fclose(f); return -1; }
        if (imm)
            norn_dht_restore_immutable(vbuf, vl);
        else
            norn_dht_restore_mutable(target, pk, seq, vbuf, vl, sig);
    }
    fclose(f);
    return 0;
}