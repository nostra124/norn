/* SPDX-License-Identifier: MIT */
#include "bep44.h"
#include "sha1.h"
#include <sodium.h>

#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

void bep44_target(const unsigned char pubkey[32], unsigned char target[20]) {
    sha1(pubkey, 32, target);
}

void bep44_target_for_pubkey(unsigned char target[20], const unsigned char pubkey[32]) {
    /* BEP-44 mutable: target = SHA1("k" || pubkey) */
    unsigned char buf[1 + 32] = {'k'};
    memcpy(buf + 1, pubkey, 32);
    sha1(buf, 33, target);
}

void bep44_target_salted(const unsigned char pubkey[32],
                         const unsigned char *salt, size_t saltlen,
                         unsigned char target[20]) {
    if (saltlen > 64) saltlen = 64;
    unsigned char buf[32 + 64];
    memcpy(buf, pubkey, 32);
    if (salt && saltlen) memcpy(buf + 32, salt, saltlen);
    sha1(buf, 32 + saltlen, target);
}

int bep44_immutable_target(const unsigned char *v, size_t vlen, unsigned char target[20]) {
    /* BEP-44 immutable: target = SHA1(bencode(v)); a byte string bencodes to
     * "<vlen>:<v>". BEP-44 caps the value at 1000 bytes — REJECT anything larger
     * (return -1) rather than truncate-and-hash, which made two values sharing
     * their first 1000 bytes alias to the same target (BUG-059). */
    if (vlen > 1000) return -1;
    unsigned char buf[16 + 1000];
    int hn = snprintf((char *)buf, 16, "%zu:", vlen);
    memcpy(buf + hn, v, vlen);
    sha1(buf, (size_t)hn + vlen, target);
    return 0;
}

int bep44_signbuf_salted(const unsigned char *salt, size_t saltlen, uint32_t seq,
                         const unsigned char *value, size_t vlen,
                         unsigned char *out, size_t outcap) {
    /* BEP-44 canonical bytes (salt optional, in order):
     * "4:salt" <saltlen> ":" <salt> "3:seqi" <seq> "e1:v" <vlen> ":" <value> */
    size_t o = 0; char tmp[64]; int n;
    if (salt && saltlen) {
        if (saltlen > 64) saltlen = 64;
        n = snprintf(tmp, sizeof(tmp), "4:salt%zu:", saltlen);
        if (n < 0 || o + (size_t)n + saltlen > outcap) return -1; /* LCOV_EXCL_BR_LINE: n<0 unreachable (fixed format into 64B tmp) */
        memcpy(out + o, tmp, (size_t)n); o += (size_t)n;
        memcpy(out + o, salt, saltlen); o += saltlen;
    }
    n = snprintf(tmp, sizeof(tmp), "3:seqi%lue1:v%zu:", (unsigned long)seq, vlen);
    if (n < 0 || o + (size_t)n + vlen > outcap) return -1; /* LCOV_EXCL_BR_LINE: n<0 unreachable (fixed format into 64B tmp) */
    memcpy(out + o, tmp, (size_t)n); o += (size_t)n;
    memcpy(out + o, value, vlen); o += vlen;
    return (int)o;
}

int bep44_signbuf(uint32_t seq, const unsigned char *value, size_t vlen,
                  unsigned char *out, size_t outcap) {
    return bep44_signbuf_salted(NULL, 0, seq, value, vlen, out, outcap);
}

int bep44_record_encode(const bep44_record_t *r, unsigned char *out, size_t outcap) {
    size_t vl = strlen(r->version);
    if (vl > 23) vl = 23;
    size_t need = 1 + vl + 4 + 2 + 16 + 4 + 32 + 20 + 6;   /* +6: FEAT-031 route-hint ip(4)+port(2) */
    int have_svc = (r->nsvc > 0 && r->svc_len > 0 && r->svc_len <= BEP44_REC_SVC_MAX);
    if (have_svc) need += 1 + r->svc_len;                  /* FEAT-035 trailing services: nsvc(1) + blob */
    if (need > outcap) return -1;
    size_t o = 0;
    out[o++] = (unsigned char)vl;
    memcpy(out + o, r->version, vl); o += vl;
    memcpy(out + o, &r->ip, 4); o += 4;
    uint16_t bp = htons(r->port); memcpy(out + o, &bp, 2); o += 2;
    memcpy(out + o, r->ula, 16); o += 16;
    uint32_t bc = htonl(r->caps); memcpy(out + o, &bc, 4); o += 4;
    memcpy(out + o, r->host_pubkey, 32); o += 32;
    memcpy(out + o, r->node_id, 20); o += 20;
    /* FEAT-031 route-hint (trailing; older readers stop after node_id) */
    memcpy(out + o, &r->route_ip, 4); o += 4;
    uint16_t rp = htons(r->route_port); memcpy(out + o, &rp, 2); o += 2;
    /* FEAT-035 services (trailing + optional; omitted entirely when none, so a
     * service-less record is byte-identical to the pre-0.22 format). */
    if (have_svc) {
        out[o++] = r->nsvc;
        memcpy(out + o, r->svc, r->svc_len); o += r->svc_len;
    }
    return (int)o;
}

int bep44_record_decode(const unsigned char *in, size_t len, bep44_record_t *r) {
    if (!in || len < 1) return -1;
    memset(r, 0, sizeof(*r));
    size_t o = 0, vl = in[o++];
    if (vl > 23 || o + vl + 4 + 2 + 16 + 4 + 32 > len) return -1;
    memcpy(r->version, in + o, vl); r->version[vl] = '\0'; o += vl;
    memcpy(&r->ip, in + o, 4); o += 4;
    uint16_t bp; memcpy(&bp, in + o, 2); r->port = ntohs(bp); o += 2;
    memcpy(r->ula, in + o, 16); o += 16;
    uint32_t bc; memcpy(&bc, in + o, 4); r->caps = ntohl(bc); o += 4;
    memcpy(r->host_pubkey, in + o, 32); o += 32;
    /* node_id is appended in newer records; older ones lack it (stays zeroed). */
    if (o + 20 <= len) { memcpy(r->node_id, in + o, 20); o += 20; }
    /* FEAT-031 route-hint, trailing + optional (older records lack it → stays zeroed). */
    if (o + 6 <= len) {
        memcpy(&r->route_ip, in + o, 4); o += 4;
        uint16_t rp; memcpy(&rp, in + o, 2); r->route_port = ntohs(rp); o += 2;
    }
    /* FEAT-035 services: trailing + optional. nsvc(1) then the remaining bytes are
     * the opaque svc blob (older records lack this → nsvc/svc_len stay zeroed). */
    if (o + 1 <= len) {
        r->nsvc = in[o++];
        size_t remain = len - o;
        if (remain > BEP44_REC_SVC_MAX) remain = BEP44_REC_SVC_MAX;
        memcpy(r->svc, in + o, remain); r->svc_len = (uint16_t)remain; o += remain;
    }
    return 0;
}

int bep44_encode(unsigned char *out, size_t outcap,
                 const unsigned char pk[32],
                 const unsigned char *value, size_t vlen,
                 uint32_t seq,
                 const unsigned char sk[64]) {
    if (!out || !pk || !value || !sk) return -1;
    if (vlen > 1000) return -1;
    
    /* Build sign buffer */
    unsigned char signbuf[2048];
    int signlen = bep44_signbuf(seq, value, vlen, signbuf, sizeof(signbuf));
    if (signlen < 0) return -1; /* LCOV_EXCL_LINE */ /* LCOV_EXCL_BR_LINE: vlen<=1000 always fits 2048B signbuf */

    /* Sign with ed25519 */
    unsigned char sig[64];
    if (crypto_sign_detached(sig, NULL, signbuf, (size_t)signlen, sk) != 0) /* LCOV_EXCL_BR_LINE: ed25519 sign never fails */
        return -1; /* LCOV_EXCL_LINE: ed25519 sign never fails */
    
    /* Output format: pk(32) || seq(4,BE) || vlen(2,BE) || value || sig(64) */
    size_t need = 32 + 4 + 2 + vlen + 64;
    if (outcap < need) return -1;
    
    size_t o = 0;
    memcpy(out + o, pk, 32); o += 32;
    uint32_t seq_be = htonl(seq);
    memcpy(out + o, &seq_be, 4); o += 4;
    uint16_t vlen_be = htons((uint16_t)vlen);
    memcpy(out + o, &vlen_be, 2); o += 2;
    memcpy(out + o, value, vlen); o += vlen;
    memcpy(out + o, sig, 64); o += 64;
    
    return (int)o;
}

int bep44_decode(const unsigned char *in, size_t len,
                 unsigned char pk[32],
                 unsigned char **value, size_t *vlen,
                 uint32_t *seq,
                 unsigned char sig[64]) {
    if (!in || len < 32 + 4 + 2 + 64) return -1;
    
    size_t o = 0;
    if (pk) { memcpy(pk, in + o, 32); } o += 32;
    uint32_t seq_be; memcpy(&seq_be, in + o, 4); o += 4;
    if (seq) *seq = ntohl(seq_be);
    uint16_t vlen_be; memcpy(&vlen_be, in + o, 2); o += 2;
    size_t vl = ntohs(vlen_be);
    if (o + vl + 64 > len) return -1;
    if (value) *value = (unsigned char *)(in + o);
    if (vlen) *vlen = vl;
    o += vl;
    if (sig) memcpy(sig, in + o, 64);
    
    /* Verify signature */
    unsigned char signbuf[2048];
    int signlen = bep44_signbuf(ntohl(seq_be), in + 32 + 4 + 2, vl, signbuf, sizeof(signbuf));
    if (signlen < 0) return -1;
    
    unsigned char sig_copy[64];
    memcpy(sig_copy, in + o, 64);
    if (crypto_sign_verify_detached(sig_copy, signbuf, (size_t)signlen, pk ? pk : in) != 0)
        return -1;
    
    return 0;
}
