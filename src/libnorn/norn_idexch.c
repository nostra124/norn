/* SPDX-License-Identifier: MIT */
/**
 * @file norn_idexch.c
 * @brief Generic identity exchange implementation.
 */

#include "norn_idexch.h"
#include <string.h>

int norn_idexch_is(const unsigned char *buf, size_t len) {
    if (!buf) return 0;
    return len >= 4 &&
           buf[0] == NORN_IDEXCH_MAGIC0 &&
           buf[1] == NORN_IDEXCH_MAGIC1 &&
           buf[2] == NORN_IDEXCH_MAGIC2 &&
           buf[3] == NORN_IDEXCH_MAGIC3;
}

int norn_idexch_build(unsigned char type,
                      const unsigned char nonce[NORN_IDEXCH_NONCE_LEN],
                      const unsigned char pubkey[], const unsigned char secret[],
                      const uint32_t *endpoint_ip, uint16_t endpoint_port,
                      const unsigned char payload[], size_t paylen,
                      unsigned char out[], size_t outcap,
                      const norn_crypto_suite_t *suite) {
    if (!nonce || !pubkey || !secret || !out || !suite) return -1;
    if (paylen > 0 && !payload) return -1;
    if (paylen > NORN_IDEXCH_PAYLOAD_MAX) return -1;
    
    size_t pubkey_len = suite->pubkey_len;
    size_t sig_len = suite->sig_len;
    
    /* Calculate required size */
    size_t need = 4 +                     /* magic */
                  1 +                     /* type */
                  NORN_IDEXCH_NONCE_LEN + /* nonce */
                  pubkey_len +            /* pubkey */
                  4 +                     /* endpoint_ip (optional) */
                  2 +                     /* endpoint_port */
                  2 +                     /* paylen */
                  paylen +                /* payload */
                  sig_len;                /* signature */
    
    if (outcap < need) return -1;
    
    unsigned char *p = out;
    
    /* Magic */
    *p++ = NORN_IDEXCH_MAGIC0;
    *p++ = NORN_IDEXCH_MAGIC1;
    *p++ = NORN_IDEXCH_MAGIC2;
    *p++ = NORN_IDEXCH_MAGIC3;
    
    /* Start of signed data */
    unsigned char *signed_start = p;
    
    /* Type */
    *p++ = type;
    
    /* Nonce */
    memcpy(p, nonce, NORN_IDEXCH_NONCE_LEN);
    p += NORN_IDEXCH_NONCE_LEN;
    
    /* Public key */
    memcpy(p, pubkey, pubkey_len);
    p += pubkey_len;
    
    /* Endpoint IP (0.0.0.0 if not provided) */
    if (endpoint_ip) {
        memcpy(p, endpoint_ip, 4);
    } else {
        memset(p, 0, 4);
    }
    p += 4;
    
    /* Endpoint port (0 if not provided) */
    *p++ = (unsigned char)(endpoint_port >> 8);
    *p++ = (unsigned char)(endpoint_port & 0xFF);
    
    /* Payload length */
    *p++ = (unsigned char)(paylen >> 8);
    *p++ = (unsigned char)(paylen & 0xFF);
    
    /* Payload */
    if (paylen > 0) {
        memcpy(p, payload, paylen);
        p += paylen;
    }
    
    /* Signature */
    size_t signed_len = (size_t)(p - signed_start);
    int ret = suite->sign(p, signed_start, signed_len, secret);
    if (ret != 0) return -1;
    p += sig_len;
    
    return (int)(p - out);
}

int norn_idexch_parse(const unsigned char buf[], size_t len,
                      unsigned char *type,
                      unsigned char nonce[NORN_IDEXCH_NONCE_LEN],
                      unsigned char pubkey[],
                      uint32_t *endpoint_ip, uint16_t *endpoint_port,
                      unsigned char payload[], size_t paycap, size_t *paylen,
                      const norn_crypto_suite_t *suite) {
    if (!norn_idexch_is(buf, len) || !suite) return -1;
    
    size_t pubkey_len = suite->pubkey_len;
    size_t sig_len = suite->sig_len;
    
    /* Minimum size: magic + type + nonce + pubkey + ip + port + paylen + sig */
    size_t min_size = 4 + 1 + NORN_IDEXCH_NONCE_LEN + pubkey_len + 4 + 2 + 2 + sig_len;
    if (len < min_size) return -1;
    
    const unsigned char *p = buf + 4;  /* Skip magic */
    const unsigned char *end = buf + len;
    
    /* Start of signed data */
    const unsigned char *signed_start = p;
    
    /* Type */
    unsigned char t = *p++;
    
    /* Nonce */
    const unsigned char *non = p;
    p += NORN_IDEXCH_NONCE_LEN;
    
    /* Public key */
    if ((size_t)(end - p) < pubkey_len) return -1;
    const unsigned char *pk = p;
    p += pubkey_len;
    
    /* Endpoint IP */
    if ((size_t)(end - p) < 4) return -1;
    uint32_t ip;
    memcpy(&ip, p, 4);
    p += 4;
    
    /* Endpoint port */
    if ((size_t)(end - p) < 2) return -1;
    uint16_t port = ((uint16_t)p[0] << 8) | p[1];
    p += 2;
    
    /* Payload length */
    if ((size_t)(end - p) < 2) return -1;
    size_t plen = ((size_t)p[0] << 8) | p[1];
    p += 2;
    
    /* Check payload fits */
    if (plen > NORN_IDEXCH_PAYLOAD_MAX) return -1;
    if ((size_t)(end - p) < plen + sig_len) return -1;
    if (payload && paycap > 0 && plen > paycap) return -1;
    
    /* Payload */
    const unsigned char *pay = p;
    p += plen;
    
    /* Signature */
    const unsigned char *sig = p;
    
    /* Verify signature */
    size_t signed_len = (size_t)(sig - signed_start);
    int ret = suite->verify(sig, signed_start, signed_len, pk);
    if (ret != 0) return -1;
    
    /* Fill outputs */
    if (type) *type = t;
    if (nonce) memcpy(nonce, non, NORN_IDEXCH_NONCE_LEN);
    if (pubkey) memcpy(pubkey, pk, pubkey_len);
    if (endpoint_ip) *endpoint_ip = ip;
    if (endpoint_port) *endpoint_port = port;
    if (paylen) *paylen = plen;
    if (payload && paycap > 0 && plen > 0) {
        memcpy(payload, pay, plen < paycap ? plen : paycap);
    }
    
    return 0;
}