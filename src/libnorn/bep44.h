#ifndef BIFROST_BEP44_H
#define BIFROST_BEP44_H

#include <stdint.h>
#include <stddef.h>

#define BEP44_REC_SVC_MAX 140   /* FEAT-035: cap on the trailing services blob (fits RECSTORE_VMAX) */

/* BEP-44 mutable items (FEAT-022). A bifrost node publishes a small signed record
 * — its current reachability + capabilities — under target = SHA1(its ed25519
 * pubkey), so a peer that already knows the pubkey can fetch the latest endpoint
 * even while the node is offline (any DHT node holding the item answers). */

typedef struct {
    char          version[24];
    uint32_t      ip;          /* reflexive endpoint (network byte order) */
    uint16_t      port;
    unsigned char ula[16];
    uint32_t      caps;        /* capability bitmask (idexch.h CAP_*) */
    unsigned char host_pubkey[32]; /* the node's SSH HOST ed25519 key (0 if none),
                                    * attested by the identity key k that signs this
                                    * record — so a fetcher can verify the host key */
    unsigned char node_id[20];     /* SHA256(account)[:20] — lets a holder resolve
                                    * this node BY ACCOUNT (it computes the same
                                    * hash) without the account ever being shared.
                                    * A one-way hash: reveals nothing about the account. */
    /* FEAT-031 (0.17) route-hint: "reach me via this hub" — the endpoint of a relay/anchor hub
     * that forwards to me when I'm inbound-blocked. 0 = none. The initiator source-routes its
     * session through this hub (the Lightning route-hint model). */
    uint32_t      route_ip;
    uint16_t      route_port;
    /* FEAT-035 (0.22) service advertisements: an OPTIONAL trailing section carrying
     * the services this node publishes (servicestore svc_encode entries). The blob is
     * opaque to bep44 — the daemon fills/parses it with servicestore's codec. Trailing
     * + optional: a record with no services encodes exactly as before (no gossip
     * churn), and older decoders simply stop before this section. */
    uint8_t       nsvc;            /* number of advertised services (0 = none) */
    uint16_t      svc_len;         /* bytes used in svc[] */
    unsigned char svc[BEP44_REC_SVC_MAX];
} bep44_record_t;

/* target = SHA1(pubkey) — the DHT key to publish under (no salt). */
void bep44_target(const unsigned char pubkey[32], unsigned char target[20]);

/* Compute target for pubkey: target = SHA1("k" || pubkey) for mutable items. */
void bep44_target_for_pubkey(unsigned char target[20], const unsigned char pubkey[32]);

/* Salted target = SHA1(pubkey || salt) — BEP-44 with salt, for per-name signed
 * DHT keys (`spub`/`sget`, salt = the key name). salt clamped to 64 bytes. */
void bep44_target_salted(const unsigned char pubkey[32],
                         const unsigned char *salt, size_t saltlen,
                         unsigned char target[20]);

/* Immutable target = SHA1(bencode(v)) — the BEP-44 content-addressed key for an
 * UNSIGNED item (`pub`/`get`). bencode(v) of a byte string is "<vlen>:<v>".
 * Returns 0 on success, or -1 if vlen > 1000 (the BEP-44 value limit) — callers
 * must reject rather than store a truncated-hash alias (BUG-059). */
int bep44_immutable_target(const unsigned char *v, size_t vlen, unsigned char target[20]);

/* Canonical buffer to sign/verify: "3:seqi<seq>e1:v<vlen>:<value>" (no salt).
 * Returns its length, or -1 on overflow. */
int bep44_signbuf(uint32_t seq, const unsigned char *value, size_t vlen,
                  unsigned char *out, size_t outcap);

/* As bep44_signbuf, but with a salt prefix per BEP-44:
 * "4:salt<saltlen>:<salt>3:seqi<seq>e1:v<vlen>:<value>". salt NULL/0 = no salt
 * (identical to bep44_signbuf). Returns length, or -1 on overflow. */
int bep44_signbuf_salted(const unsigned char *salt, size_t saltlen, uint32_t seq,
                         const unsigned char *value, size_t vlen,
                         unsigned char *out, size_t outcap);

/* Compact record value codec.
 * Wire: ver_len(1) ver ip(4) port(2,BE) ula(16) caps(4,BE) host_pubkey(32) node_id(20).
 * node_id is optional on decode (older records lack it → left zeroed).
 * encode returns bytes written (or -1); decode returns 0 (or -1). */
int bep44_record_encode(const bep44_record_t *r, unsigned char *out, size_t outcap);
int bep44_record_decode(const unsigned char *in, size_t len, bep44_record_t *r);

/* BEP-44 mutable item encode/decode with signature.
 * encode: sign the value with sk and produce the wire format.
 * decode: verify signature with pk and extract value.
 * Returns bytes/0 on success, -1 on error. */
int bep44_encode(unsigned char *out, size_t outcap,
                 const unsigned char pk[32],
                 const unsigned char *value, size_t vlen,
                 uint32_t seq,
                 const unsigned char sk[64]);
int bep44_decode(const unsigned char *in, size_t len,
                 unsigned char pk[32],
                 unsigned char **value, size_t *vlen,
                 uint32_t *seq,
                 unsigned char sig[64]);

#endif
