#ifndef BIFROST_RECSTORE_H
#define BIFROST_RECSTORE_H

#include <stdint.h>
#include <stddef.h>

/* The shared store of signed records (BPE-0004). One validating gate that every
 * distribution path — BEP-44 DHT put/get and gossip — funnels through, so a
 * stale/forged/replayed record is rejected once, for all callers. */

#define RECSTORE_MAX  256
#define RECSTORE_VMAX 256   /* max record value bytes (ours ~51; BEP-44 cap 1000) */

typedef struct {
    unsigned char target[20];   /* SHA1(k) */
    unsigned char k[32];        /* publisher ed25519 identity pubkey */
    uint32_t      seq;          /* monotonic */
    unsigned char v[RECSTORE_VMAX];
    size_t        vlen;
    unsigned char sig[64];      /* ed25519 over the BEP-44 canonical buffer */
    long          last_seen;
    char          via[64];      /* account of the peer that last forwarded this to us
                                 * (a direct peer of ours); "" if from DHT/own. In-memory
                                 * only (not persisted) — it's "who currently provides it". */
    int           priv;         /* FEAT-048 (0.17): 1 = arrived via PRIVATE trusted gossip (or is
                                 * our own record while stealth). Excluded from public gossip, the
                                 * mainline DHT, and the on-disk store — re-learned from trusted
                                 * peers each session. In-memory only. */
} rec_t;

/* Load from path (write-through afterwards). Returns count loaded, or -1. */
int recstore_init(const char *path);

/* The gate: verify sig against k, require SHA1(k)==target, and seq strictly
 * newer than any held for this target; then store/replace. Returns 1 if accepted
 * (new or newer), 0 if rejected (bad sig / stale / dup / full). */
int recstore_accept(const unsigned char k[32], uint32_t seq,
                    const unsigned char *v, size_t vlen, const unsigned char sig[64]);

/* Fetch by target, or by pubkey (target = SHA1(k)). 1 + fills out, else 0. */
int recstore_get(const unsigned char target[20], rec_t *out);
int recstore_get_by_pubkey(const unsigned char k[32], rec_t *out);
/* Resolve by node_id = SHA256(account)[:20], matched against the node_id field
 * embedded in each record's value. Lets a holder find a gossiped peer by account
 * (it hashes the account) without the account ever being shared. 1 + fills out. */
int recstore_get_by_node_id(const unsigned char node_id[20], rec_t *out);

/* Note who forwarded us the record for pubkey k (a direct peer's account) — set on
 * every gossip receipt so `peers`/`gossip` can show "via <peer>" for indirect
 * peers. No-op if we don't hold the record. */
void recstore_set_via(const unsigned char k[32], const char *via);

/* FEAT-048: mark the record for pubkey k as private (1) or public (0). Private
 * records are kept in memory for resolution but never written to disk, published
 * to the mainline DHT, or gossiped to non-trusted peers. No-op if not held. */
void recstore_set_private(const unsigned char k[32], int val);

int recstore_count(void);
int recstore_list(rec_t *out, int max);   /* for gossip digests */

#endif
