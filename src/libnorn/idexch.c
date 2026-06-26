#include "idexch.h"
#include "crypto.h"

#include <string.h>

#define SIG_LEN 64

int idexch_is(const unsigned char *buf, size_t len) {
    return len >= 5 && buf[0] == IDEXCH_MAGIC0 && buf[1] == IDEXCH_MAGIC1 &&
           buf[2] == IDEXCH_MAGIC2 && buf[3] == IDEXCH_MAGIC3;
}

int idexch_build(unsigned char type, const unsigned char nonce[16],
                 const unsigned char pub[32], const unsigned char sk[64],
                 const unsigned char ula[16], const char *version,
                 const char *account, const unsigned char *payload, size_t paylen,
                 unsigned char *out, size_t outcap) {
    if (!nonce || !pub || !sk || !ula || !version || !account || !out) return -1;
    if (paylen && !payload) return -1;
    size_t vlen = strlen(version), alen = strlen(account);
    if (vlen > 255 || alen > 255 || paylen > IDEXCH_PAYLOAD_MAX) return -1;
    size_t need = 4 + 1 + 16 + 32 + 16 + 1 + vlen + 1 + alen + 2 + paylen + SIG_LEN;
    if (outcap < need) return -1;

    unsigned char *p = out;
    *p++ = IDEXCH_MAGIC0; *p++ = IDEXCH_MAGIC1; *p++ = IDEXCH_MAGIC2; *p++ = IDEXCH_MAGIC3;
    unsigned char *signed_start = p;          /* sign type .. payload (excl. sig) */
    *p++ = type;
    memcpy(p, nonce, 16); p += 16;
    memcpy(p, pub, 32);   p += 32;
    memcpy(p, ula, 16);   p += 16;
    *p++ = (unsigned char)vlen; memcpy(p, version, vlen); p += vlen;
    *p++ = (unsigned char)alen; memcpy(p, account, alen); p += alen;
    *p++ = (unsigned char)(paylen >> 8); *p++ = (unsigned char)(paylen & 0xff);
    if (paylen) { memcpy(p, payload, paylen); p += paylen; }
    if (bf_sign(p, signed_start, (size_t)(p - signed_start), sk) != 0) return -1;   /* LCOV_EXCL_BR_LINE: ed25519 sign never fails */
    p += SIG_LEN;
    return (int)(p - out);
}

int idexch_parse(const unsigned char *buf, size_t len,
                 unsigned char *type, unsigned char nonce[16],
                 unsigned char pub[32], unsigned char ula[16],
                 char *version, size_t vcap, char *account, size_t acap,
                 unsigned char *payload, size_t paycap, size_t *paylen) {
    if (!idexch_is(buf, len)) return -1;
    const unsigned char *p = buf + 4;
    const unsigned char *end = buf + len;
    if (end - p < 1 + 16 + 32 + 16 + 1) return -1;

    const unsigned char *signed_start = p;
    unsigned char t = *p++;
    const unsigned char *non = p; p += 16;
    const unsigned char *pk  = p; p += 32;
    const unsigned char *ul  = p; p += 16;

    size_t vlen = *p++;
    if ((size_t)(end - p) < vlen + 1) return -1;
    const unsigned char *ver = p; p += vlen;
    size_t alen = *p++;
    if ((size_t)(end - p) < alen + 2) return -1;
    const unsigned char *acct = p; p += alen;
    size_t plen = ((size_t)p[0] << 8) | p[1]; p += 2;
    if ((size_t)(end - p) < plen + SIG_LEN) return -1;
    /* A payload that won't fit the caller's buffer is malformed: copying min()
     * while reporting the full plen via *paylen desyncs callers into an OOB read
     * of their buffer (BUG-029). Reject it. */
    if (payload && paycap && plen > paycap) return -1;
    const unsigned char *pay = p; p += plen;
    const unsigned char *sig = p;

    if (bf_verify(sig, signed_start, (size_t)(sig - signed_start), pk) != 0) return -1;

    if (type)  *type = t;
    if (nonce) memcpy(nonce, non, 16);
    if (pub)   memcpy(pub, pk, 32);
    if (ula)   memcpy(ula, ul, 16);
    if (version && vcap) { size_t n = vlen < vcap - 1 ? vlen : vcap - 1; memcpy(version, ver, n); version[n] = '\0'; }
    if (account && acap) { size_t n = alen < acap - 1 ? alen : acap - 1; memcpy(account, acct, n); account[n] = '\0'; }
    if (paylen) *paylen = plen;
    if (payload && paycap) { size_t n = plen < paycap ? plen : paycap; memcpy(payload, pay, n); }
    return 0;
}
