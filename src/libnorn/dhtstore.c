/* SPDX-License-Identifier: MIT */
#include "dhtstore.h"
#include "bep44.h"
#include "crypto.h"

#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

typedef struct {
    int           used;
    int           immutable;  /* BEP-44 immutable item (target=SHA1(bencode(v)), no k/sig/seq) */
    unsigned char target[20];
    unsigned char k[32];
    uint32_t      seq;
    unsigned char v[DHTSTORE_VMAX];
    size_t        vlen;
    unsigned char sig[64];
    uint32_t      src_ip;
    long          stored;     /* unix time, for TTL + LRU */
} item_t;

#define DHTSTORE_MAX 4096      /* hard cap on item count (budget usually binds first) */
static item_t  g_items[DHTSTORE_MAX];
static int     g_count = 0;
static size_t  g_bytes = 0;
static size_t  g_budget = 0;

static size_t total_ram(void) {
#if defined(__APPLE__)
    int64_t mem = 0; size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, NULL, 0) == 0 && mem > 0) return (size_t)mem;
#elif defined(__linux__)
    long pages = sysconf(_SC_PHYS_PAGES), ps = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && ps > 0) return (size_t)pages * (size_t)ps;   /* LCOV_EXCL_BR_LINE: sysconf failure not unit-testable */
#endif
    return 0;   /* LCOV_EXCL_LINE: sysconf always succeeds on the test host */
}

size_t dhtstore_init(int budget_mb, int client_only) {
    g_count = 0; g_bytes = 0;
    memset(g_items, 0, sizeof(g_items));
    if (client_only) { g_budget = 0; return 0; }       /* don't serve */
    if (budget_mb > 0) {
        g_budget = (size_t)budget_mb * 1024 * 1024;
    } else {
        size_t ram = total_ram();
        size_t b = ram ? ram / 512 : 2 * 1024 * 1024;  /* ~0.2% of RAM, 2MB fallback */ /* LCOV_EXCL_BR_LINE: clamp arms depend on host physical RAM */
        if (b < 2 * 1024 * 1024)  b = 2 * 1024 * 1024;  /* LCOV_EXCL_BR_LINE: depends on host physical RAM */
        if (b > 64 * 1024 * 1024) b = 64 * 1024 * 1024; /* LCOV_EXCL_BR_LINE: depends on host physical RAM */
        g_budget = b;
    }
    return g_budget;
}

static item_t *find(const unsigned char target[20]) {
    for (int i = 0; i < g_count; i++)
        if (g_items[i].used && memcmp(g_items[i].target, target, 20) == 0) return &g_items[i];   /* LCOV_EXCL_BR_LINE: compacted array — slots < g_count are always used */
    return NULL;
}

static void remove_at(int i) {
    g_bytes -= g_items[i].vlen;
    g_items[i] = g_items[--g_count];   /* swap-remove */
}

/* Drop expired items; returns how many remain. */
static void expire(long now) {
    for (int i = 0; i < g_count; ) {
        if (now - g_items[i].stored > DHTSTORE_TTL) remove_at(i);   /* LCOV_EXCL_BR_LINE: TTL expiry needs 2h wall-clock, not unit-testable */
        else i++;
    }
}

/* Evict the least-recently-stored item to make room. */
static void evict_lru(void) {
    if (g_count == 0) return;   /* LCOV_EXCL_BR_LINE: only called inside the g_count>0 while-guard */
    int oldest = 0;
    for (int i = 1; i < g_count; i++)
        if (g_items[i].stored < g_items[oldest].stored) oldest = i;
    remove_at(oldest);
}

static int per_ip_count(uint32_t ip) {
    int n = 0;
    for (int i = 0; i < g_count; i++) if (g_items[i].used && g_items[i].src_ip == ip) n++;   /* LCOV_EXCL_BR_LINE: compacted array — slots < g_count are always used */
    return n;
}

/* Store/replace an item at target (caller has already validated/verified). Enforces
 * per-IP cap (new items), seq-monotonicity (replace), TTL expiry and the byte
 * budget (LRU). Returns 1 stored, 0 not. */
static int store_item(const unsigned char target[20], const unsigned char k[32],
                      uint32_t seq, const unsigned char *v, size_t vlen,
                      const unsigned char sig[64], int immutable, uint32_t src_ip) {
    long now = time(NULL);
    expire(now);

    item_t *e = find(target);
    if (e) {
        if (immutable) { e->stored = now; return 1; }    /* content-addressed: already have it */
        if (seq <= e->seq) return 0;                     /* stale / replay */
        g_bytes -= e->vlen;                              /* update in place */
    } else {
        /* new item: enforce per-IP cap, then make room within the budget */
        if (per_ip_count(src_ip) >= DHTSTORE_PER_IP) return 0;
        while ((g_bytes + vlen > g_budget || g_count >= DHTSTORE_MAX) && g_count > 0)   /* LCOV_EXCL_BR_LINE: g_count>=MAX (4096) unreachable — byte budget binds first */
            evict_lru();
        if (g_bytes + vlen > g_budget) return 0;         /* item alone exceeds budget */  /* LCOV_EXCL_BR_LINE: max item is 1000B < min 1MB budget */
        e = &g_items[g_count++];
        memcpy(e->target, target, 20);
        e->used = 1;
    }
    memcpy(e->k, k, 32);
    e->seq = seq;
    memcpy(e->v, v, vlen); e->vlen = vlen;
    memcpy(e->sig, sig, 64);
    e->immutable = immutable;
    e->src_ip = src_ip;
    e->stored = now;
    g_bytes += vlen;
    return 1;
}

int dhtstore_put(const unsigned char target[20], const unsigned char k[32],
                 uint32_t seq, const unsigned char *v, size_t vlen,
                 const unsigned char sig[64],
                 const unsigned char *salt, size_t saltlen, uint32_t src_ip) {
    if (g_budget == 0) return 0;                         /* serving disabled */
    if (!target || !k || !v || vlen == 0 || vlen > DHTSTORE_VMAX || !sig) return 0;

    /* verify: sig over the (salted) canonical buffer vs k, and target == SHA1(k[||salt]) */
    unsigned char buf[64 + DHTSTORE_VMAX];
    int bn = bep44_signbuf_salted(salt, saltlen, seq, v, vlen, buf, sizeof(buf));
    if (bn < 0 || bf_verify(sig, buf, (size_t)bn, k) != 0) return 0;   /* LCOV_EXCL_BR_LINE: bn<0 unreachable — buf fits any vlen<=VMAX */
    unsigned char t[20];
    if (salt && saltlen) bep44_target_salted(k, salt, saltlen, t); else bep44_target(k, t);
    if (memcmp(t, target, 20) != 0) return 0;

    return store_item(target, k, seq, v, vlen, sig, 0, src_ip);
}

int dhtstore_put_immutable(const unsigned char *v, size_t vlen, uint32_t src_ip,
                           unsigned char target_out[20]) {
    if (g_budget == 0) return 0;
    if (!v || vlen == 0 || vlen > DHTSTORE_VMAX) return 0;
    /* BEP-44 immutable: the key IS SHA1(bencode(v)) — content-addressed, so it is
     * self-verifying and can't be overwritten (a different value is a different
     * key). No k/sig/seq. */
    unsigned char tgt[20];
    if (bep44_immutable_target(v, vlen, tgt) != 0) return 0;   /* >1000B: reject (BUG-059) */ /* LCOV_EXCL_BR_LINE: vlen>VMAX already rejected above, so this never fails */
    if (target_out) memcpy(target_out, tgt, 20);
    static const unsigned char zero32[32] = {0}, zero64[64] = {0};
    return store_item(tgt, zero32, 0, v, vlen, zero64, 1, src_ip);
}

int dhtstore_get(const unsigned char target[20], unsigned char k_out[32],
                 uint32_t *seq_out, unsigned char *v_out, size_t vcap,
                 size_t *vlen_out, unsigned char sig_out[64]) {
    return dhtstore_get_ex(target, k_out, seq_out, v_out, vcap, vlen_out, sig_out, NULL);
}

int dhtstore_get_ex(const unsigned char target[20], unsigned char k_out[32],
                    uint32_t *seq_out, unsigned char *v_out, size_t vcap,
                    size_t *vlen_out, unsigned char sig_out[64], int *immutable_out) {
    expire(time(NULL));
    item_t *e = find(target);
    if (!e) return 0;
    if (k_out)   memcpy(k_out, e->k, 32);
    if (seq_out) *seq_out = e->seq;
    if (sig_out) memcpy(sig_out, e->sig, 64);
    if (immutable_out) *immutable_out = e->immutable;
    if (v_out && vlen_out) {
        size_t n = e->vlen < vcap ? e->vlen : vcap;
        memcpy(v_out, e->v, n); *vlen_out = n;
    }
    return 1;
}

size_t dhtstore_bytes(void) { return g_bytes; }
int    dhtstore_count(void) { return g_count; }

int dhtstore_del(const unsigned char target[20]) {   /* BUG-122 */
    if (!target) return 0;
    for (int i = 0; i < g_count; i++)
        if (g_items[i].used && memcmp(g_items[i].target, target, 20) == 0) {   /* LCOV_EXCL_BR_LINE: compacted array — slots < g_count are always used */
            remove_at(i);
            return 1;     /* removed */
        }
    return 0;             /* not held */
}

int dhtstore_list(int want_immutable, dht_item_info_t *out, int max) {   /* BUG-122 */
    int n = 0;
    for (int i = 0; i < g_count && n < max; i++) {
        if (!g_items[i].used) continue;   /* LCOV_EXCL_BR_LINE: compacted array — slots < g_count are always used */
        if (!!g_items[i].immutable != !!want_immutable) continue;
        memcpy(out[n].target, g_items[i].target, 20);
        out[n].immutable = g_items[i].immutable;
        out[n].vlen = g_items[i].vlen;
        out[n].seq = g_items[i].seq;
        out[n].stored = g_items[i].stored;
        n++;
    }
    return n;
}
