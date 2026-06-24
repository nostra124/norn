/* Public-domain SHA-1 (after Steve Reid's implementation), trimmed to a
 * one-shot sha1(). Used ONLY for BEP-44 mutable-item targets. */
#include "sha1.h"
#include <string.h>

#define ROL(v, b) (((v) << (b)) | ((v) >> (32 - (b))))

typedef struct { uint32_t state[5]; uint64_t count; unsigned char buf[64]; } sha1_ctx;

static void sha1_transform(uint32_t state[5], const unsigned char buffer[64]) {
    uint32_t a, b, c, d, e, w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)buffer[i*4] << 24) | ((uint32_t)buffer[i*4+1] << 16) |
               ((uint32_t)buffer[i*4+2] << 8) | (uint32_t)buffer[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | ((~b) & d);      k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                 k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                 k = 0xCA62C1D6; }
        uint32_t t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void sha1(const unsigned char *data, size_t len, unsigned char out[20]) {
    sha1_ctx c;
    c.state[0] = 0x67452301; c.state[1] = 0xEFCDAB89; c.state[2] = 0x98BADCFE;
    c.state[3] = 0x10325476; c.state[4] = 0xC3D2E1F0; c.count = 0;

    size_t i = 0;
    /* process full 64-byte blocks straight from the input */
    while (len - i >= 64) { sha1_transform(c.state, data + i); i += 64; }
    size_t rem = len - i;
    memcpy(c.buf, data + i, rem);

    uint64_t bits = (uint64_t)len * 8;
    c.buf[rem++] = 0x80;
    if (rem > 56) { while (rem < 64) c.buf[rem++] = 0; sha1_transform(c.state, c.buf); rem = 0; }
    while (rem < 56) c.buf[rem++] = 0;
    for (int j = 7; j >= 0; j--) c.buf[rem++] = (unsigned char)(bits >> (j * 8));
    sha1_transform(c.state, c.buf);

    for (int j = 0; j < 5; j++) {
        out[j*4]   = (unsigned char)(c.state[j] >> 24);
        out[j*4+1] = (unsigned char)(c.state[j] >> 16);
        out[j*4+2] = (unsigned char)(c.state[j] >> 8);
        out[j*4+3] = (unsigned char)(c.state[j]);
    }
}
