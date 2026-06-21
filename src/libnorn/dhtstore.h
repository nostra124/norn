#ifndef BIFROST_DHTSTORE_H
#define BIFROST_DHTSTORE_H

#include <stdint.h>
#include <stddef.h>

/* Bounded, UNTRUSTED store of BEP-44 mutable items we hold for the network as a
 * good DHT citizen (BPE-0004). Distinct from recstore (our trusted records): this
 * is a volatile, capped cache — a byte budget derived from host RAM (or
 * --dht-store-budget), with per-source-IP limits, a 2 h TTL and LRU eviction.
 * Supports salted items (target = SHA1(k||salt)) for per-name signed KV (spub).
 * value cap is the BEP-44 1000-byte limit. */

#define DHTSTORE_VMAX     1000   /* BEP-44 max value size */
#define DHTSTORE_TTL      7200   /* seconds (BEP-44: items MAY expire after 2 h) */
#define DHTSTORE_PER_IP   32     /* max items a single source IP may store */

/* Initialize with a byte budget. budget_mb < 0 => auto from RAM (clamp(RAM/512,
 * 2MB, 64MB)); 0 => disabled (don't serve, e.g. client-only). Returns the
 * effective budget in bytes. */
size_t dhtstore_init(int budget_mb, int client_only);

/* Validate and store a put: verify sig over the BEP-44 (salted) canonical buffer
 * against k, require SHA1(k[||salt])==target and seq strictly newer; enforce the
 * per-IP cap and the byte budget (LRU-evicting expired/oldest items). salt
 * NULL/0 = unsalted. Returns 1 stored, 0 not. */
int dhtstore_put(const unsigned char target[20], const unsigned char k[32],
                 uint32_t seq, const unsigned char *v, size_t vlen,
                 const unsigned char sig[64],
                 const unsigned char *salt, size_t saltlen, uint32_t src_ip);

/* Store a BEP-44 IMMUTABLE value (`ipub`/`iget`): the key is SHA1(bencode(v)),
 * content-addressed and self-verifying (no k/sig/seq). Fills target_out with the
 * computed key. Returns 1 (stored or already held), 0 not. */
int dhtstore_put_immutable(const unsigned char *v, size_t vlen, uint32_t src_ip,
                           unsigned char target_out[20]);

/* Fetch by target (ignoring expired). 1 + fills the non-NULL outs, else 0. */
int dhtstore_get(const unsigned char target[20], unsigned char k_out[32],
                 uint32_t *seq_out, unsigned char *v_out, size_t vcap,
                 size_t *vlen_out, unsigned char sig_out[64]);
/* As dhtstore_get, plus *immutable_out = 1 if the held item is a BEP-44 immutable. */
int dhtstore_get_ex(const unsigned char target[20], unsigned char k_out[32],
                    uint32_t *seq_out, unsigned char *v_out, size_t vcap,
                    size_t *vlen_out, unsigned char sig_out[64], int *immutable_out);

size_t dhtstore_bytes(void);   /* current bytes held */
int    dhtstore_count(void);

/* Remove the item at target. Returns 1 if held (removed), 0 if absent (BUG-122). */
int dhtstore_del(const unsigned char target[20]);

/* Enumerate held items of one kind into out[] (up to max). want_immutable: 1 =
 * immutable (hash) items, 0 = mutable (signed) items. Returns the count (BUG-122). */
typedef struct {
    unsigned char target[20];
    int           immutable;
    size_t        vlen;
    uint32_t      seq;
    long          stored;
} dht_item_info_t;
int dhtstore_list(int want_immutable, dht_item_info_t *out, int max);

#endif
