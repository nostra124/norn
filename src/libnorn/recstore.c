#include "recstore.h"
#include "bep44.h"
#include "crypto.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>   /* fchmod */

/* In-memory table with write-through to a text file (one hex-encoded record per
 * line: target k seq vlen v sig last_seen). Single-threaded (daemon loop). */
static rec_t g_recs[RECSTORE_MAX];
static int   g_count = 0;
static char  g_path[512];

static void hexenc(const unsigned char *in, size_t n, char *out) {
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[i*2] = d[in[i] >> 4]; out[i*2+1] = d[in[i] & 0xf]; }
    out[n*2] = '\0';
}
static int hexdec(const char *in, unsigned char *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = in[i*2], e = in[i*2+1];
        if (!c || !e) return -1;
        int hi = (c <= '9') ? c-'0' : (c|0x20)-'a'+10;
        int lo = (e <= '9') ? e-'0' : (e|0x20)-'a'+10;
        if (hi < 0 || hi > 15 || lo < 0 || lo > 15) return -1;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

static rec_t *find(const unsigned char target[20]) {
    for (int i = 0; i < g_count; i++)
        if (memcmp(g_recs[i].target, target, 20) == 0) return &g_recs[i];
    return NULL;
}

static void save(void) {
    if (!g_path[0]) return;
    char tmp[600]; snprintf(tmp, sizeof(tmp), "%s.tmp", g_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fchmod(fileno(f), 0600);   /* signed records: owner-only (BUG-041) */
    for (int i = 0; i < g_count; i++) {
        if (g_recs[i].priv) continue;   /* FEAT-048: never persist private records to disk */
        char tg[41], kk[65], vv[RECSTORE_VMAX*2+1], sg[129];
        hexenc(g_recs[i].target, 20, tg);
        hexenc(g_recs[i].k, 32, kk);
        hexenc(g_recs[i].v, g_recs[i].vlen, vv);
        hexenc(g_recs[i].sig, 64, sg);
        fprintf(f, "%s %s %u %zu %s %s %ld\n", tg, kk, g_recs[i].seq,
                g_recs[i].vlen, vv, sg, g_recs[i].last_seen);
    }
    fclose(f);
    rename(tmp, g_path);
}

int recstore_init(const char *path) {
    g_count = 0;
    snprintf(g_path, sizeof(g_path), "%s", path ? path : "");
    if (!g_path[0]) return -1;
    FILE *f = fopen(g_path, "r");
    if (!f) return 0;
    char tg[64], kk[80], vv[RECSTORE_VMAX*2+8], sg[160];
    unsigned int seq; size_t vlen; long ls;
    /* Read whole lines (bounded) and parse with WIDTH-LIMITED conversions — a
     * bare %s for the value field used to overflow vv[] on a tampered/over-long
     * line. The line buffer fits a max record (tg40+kk64+v512+sig128+nums). */
    char line[RECSTORE_VMAX*2 + 512];
    while (g_count < RECSTORE_MAX && fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%63s %79s %u %zu %519s %159s %ld", tg, kk, &seq, &vlen, vv, sg, &ls) != 7)
            continue;
        if (vlen > RECSTORE_VMAX) continue;
        rec_t r; memset(&r, 0, sizeof(r));
        if (hexdec(tg, r.target, 20) || hexdec(kk, r.k, 32) ||
            hexdec(vv, r.v, vlen) || hexdec(sg, r.sig, 64)) continue;
        r.seq = seq; r.vlen = vlen; r.last_seen = ls;
        g_recs[g_count++] = r;
    }
    fclose(f);
    return g_count;
}

int recstore_accept(const unsigned char k[32], uint32_t seq,
                    const unsigned char *v, size_t vlen, const unsigned char sig[64]) {
    if (!k || !v || vlen == 0 || vlen > RECSTORE_VMAX || !sig) return 0;
    /* verify the signature over the BEP-44 canonical buffer */
    unsigned char buf[64 + RECSTORE_VMAX];
    int bn = bep44_signbuf(seq, v, vlen, buf, sizeof(buf));
    if (bn < 0 || bf_verify(sig, buf, (size_t)bn, k) != 0) return 0;

    unsigned char target[20]; bep44_target(k, target);
    rec_t *e = find(target);
    if (e && seq <= e->seq) return 0;        /* stale / replay */
    if (!e) {
        if (g_count >= RECSTORE_MAX) {
            static int warned = 0;   /* BUG-055: surface the drop once, not silently */
            if (!warned) { warned = 1; fprintf(stderr, "recstore: full (%d) — dropping new record\n", RECSTORE_MAX); }
            return 0;
        }
        e = &g_recs[g_count++];
        memcpy(e->target, target, 20);
    }
    memcpy(e->k, k, 32);
    e->seq = seq;
    memcpy(e->v, v, vlen); e->vlen = vlen;
    memcpy(e->sig, sig, 64);
    e->last_seen = time(NULL);
    e->priv = 0;   /* FEAT-048: public by default; the private receive path marks it after */
    save();
    return 1;
}

void recstore_set_private(const unsigned char k[32], int val) {
    if (!k) return;
    unsigned char target[20]; bep44_target(k, target);
    rec_t *e = find(target);
    if (e && e->priv != val) { e->priv = val; save(); }
}

void recstore_set_via(const unsigned char k[32], const char *via) {
    if (!k || !via) return;
    unsigned char target[20]; bep44_target(k, target);
    rec_t *e = find(target);
    if (e) {
        snprintf(e->via, sizeof(e->via), "%s", via);
        /* A peer just (re)gossiped this record to us — a fresh sighting even when
         * the record is a duplicate (same seq, so recstore_accept didn't re-stamp
         * it). Refresh last_seen so `bifrost gossip` SEEN reflects actual mesh
         * liveness ("last heard"), not just when we first saw this version. */
        e->last_seen = time(NULL);
    }
}

int recstore_get(const unsigned char target[20], rec_t *out) {
    rec_t *e = find(target);
    if (!e) return 0;
    if (out) *out = *e;
    return 1;
}

int recstore_get_by_pubkey(const unsigned char k[32], rec_t *out) {
    unsigned char target[20]; bep44_target(k, target);
    return recstore_get(target, out);
}

int recstore_get_by_node_id(const unsigned char node_id[20], rec_t *out) {
    /* Records don't index by node_id, so decode each value and compare its
     * embedded node_id. Lets us resolve a peer BY ACCOUNT (caller hashes the
     * account to node_id) using only what gossip propagated — the account name
     * itself is never on the wire. Skip the all-zero node_id (older records). */
    static const unsigned char zero[20] = {0};
    if (!node_id || memcmp(node_id, zero, 20) == 0) return 0;
    for (int i = 0; i < g_count; i++) {
        bep44_record_t r;
        if (bep44_record_decode(g_recs[i].v, g_recs[i].vlen, &r) != 0) continue;
        if (memcmp(r.node_id, node_id, 20) == 0) {
            if (out) *out = g_recs[i];
            return 1;
        }
    }
    return 0;
}

int recstore_count(void) { return g_count; }

int recstore_list(rec_t *out, int max) {
    if (!out || max <= 0) return 0;
    int n = g_count < max ? g_count : max;
    for (int i = 0; i < n; i++) out[i] = g_recs[i];
    return n;
}
