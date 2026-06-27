/* Unit tests for the ssh-agent signing client (FEAT-028). 100% line+branch. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"

static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

/* Build a valid SSH_AGENT_SIGN_RESPONSE *body* (no outer frame) for `sig`.
 * Returns the body length. */
static size_t build_body(const unsigned char sig[64], unsigned char *out) {
    unsigned char blob[4 + 11 + 4 + 64];
    unsigned char *b = blob;
    put32(b, 11); b += 4;
    memcpy(b, "ssh-ed25519", 11); b += 11;
    put32(b, 64); b += 4;
    memcpy(b, sig, 64); b += 64;
    size_t bloblen = (size_t)(b - blob);
    unsigned char *p = out;
    *p++ = 14; /* SSH_AGENT_SIGN_RESPONSE */
    put32(p, (uint32_t)bloblen); p += 4;
    memcpy(p, blob, bloblen); p += bloblen;
    return (size_t)(p - out);
}

static void test_encode(void) {
    unsigned char pub[32];
    memset(pub, 0x42, sizeof(pub));
    unsigned char data[64];
    memset(data, 0x7, sizeof(data));
    unsigned char out[256];
    size_t n = 0;

    /* arg guards */
    assert(nornd_agent_encode_sign_request(NULL, data, 64, out, sizeof(out), &n) == -1);
    assert(nornd_agent_encode_sign_request(pub, data, 64, NULL, sizeof(out), &n) == -1);
    assert(nornd_agent_encode_sign_request(pub, data, 64, out, sizeof(out), NULL) == -1);
    assert(nornd_agent_encode_sign_request(pub, NULL, 64, out, sizeof(out), &n) == -1);

    /* won't fit: tiny cap */
    assert(nornd_agent_encode_sign_request(pub, data, 64, out, 8, &n) == -1);
    /* exceeds the request ceiling even when the output buffer is big enough —
     * so the REQ_MAX guard (not the cap guard) is what rejects it. */
    static unsigned char big[NORND_AGENT_REQ_MAX + 256];
    static unsigned char huge[NORND_AGENT_REQ_MAX + 16];
    assert(nornd_agent_encode_sign_request(pub, huge, sizeof(huge), big,
                                           sizeof(big), &n) == -1);

    /* empty data is valid (covers the dlen==0 branch) */
    assert(nornd_agent_encode_sign_request(pub, NULL, 0, out, sizeof(out), &n) > 0);

    /* a normal request is well-formed: type byte + embedded key type */
    int len = nornd_agent_encode_sign_request(pub, data, 64, out, sizeof(out), &n);
    assert(len > 0 && (size_t)len == n);
    assert(out[4] == 13);                                 /* SIGN_REQUEST */
    assert(memcmp(out + 4 + 1 + 4 + 4, "ssh-ed25519", 11) == 0);
}

static void test_decode(void) {
    unsigned char sig[64];
    memset(sig, 0x9, sizeof(sig));
    unsigned char body[128];
    size_t blen = build_body(sig, body);
    unsigned char got[64];

    /* valid */
    assert(nornd_agent_decode_sign_response(body, blen, got) == 0);
    assert(memcmp(got, sig, 64) == 0);

    /* null args */
    assert(nornd_agent_decode_sign_response(NULL, blen, got) == -1);
    assert(nornd_agent_decode_sign_response(body, blen, NULL) == -1);
    /* empty / wrong type */
    assert(nornd_agent_decode_sign_response(body, 0, got) == -1);
    unsigned char t = body[0];
    body[0] = 0;
    assert(nornd_agent_decode_sign_response(body, blen, got) == -1);
    body[0] = t;
    /* truncated before the sigblob length */
    assert(nornd_agent_decode_sign_response(body, 1, got) == -1);
    /* sigblob length larger than what remains */
    {
        unsigned char b2[128];
        memcpy(b2, body, blen);
        put32(b2 + 1, 0xffff); /* claim a huge sigblob */
        assert(nornd_agent_decode_sign_response(b2, blen, got) == -1);
    }
    /* sigblob too short to hold the type string (left < 4) */
    {
        unsigned char b2[16];
        b2[0] = 14;
        put32(b2 + 1, 2); /* sigblob of 2 bytes */
        b2[5] = 0; b2[6] = 0;
        assert(nornd_agent_decode_sign_response(b2, 7, got) == -1);
    }
    /* type-string length runs past the sigblob (tlen > left) */
    {
        unsigned char b2[16];
        b2[0] = 14;
        put32(b2 + 1, 6);   /* sigblob = 6 bytes: a 4-byte len + 2 spare */
        put32(b2 + 5, 99);  /* tlen = 99 > left(2) */
        b2[9] = 0; b2[10] = 0;
        assert(nornd_agent_decode_sign_response(b2, 11, got) == -1);
    }
    /* wrong key type, same length (tlen==11 but bytes differ) */
    {
        unsigned char b2[128];
        size_t n = build_body(sig, b2);
        b2[1 + 4 + 4 + 10] = 'Z'; /* corrupt a byte of "ssh-ed25519" */
        assert(nornd_agent_decode_sign_response(b2, n, got) == -1);
    }
    /* key type of a different length (tlen != 11) */
    {
        unsigned char b2[128];
        unsigned char *p = b2;
        *p++ = 14;
        unsigned char blob[4 + 7 + 4 + 64];
        unsigned char *b = blob;
        put32(b, 7); b += 4; memcpy(b, "ssh-rsa", 7); b += 7;
        put32(b, 64); b += 4; memset(b, 0, 64); b += 64;
        size_t bl = (size_t)(b - blob);
        put32(p, (uint32_t)bl); p += 4;
        memcpy(p, blob, bl); p += bl;
        assert(nornd_agent_decode_sign_response(b2, (size_t)(p - b2), got) == -1);
    }
    /* no room for the signature string after the type (left < 4) */
    {
        unsigned char b2[32];
        unsigned char *p = b2;
        *p++ = 14;
        put32(p, 4 + 11); p += 4;     /* sigblob holds only the type string */
        put32(p, 11); p += 4;
        memcpy(p, "ssh-ed25519", 11); p += 11;
        assert(nornd_agent_decode_sign_response(b2, (size_t)(p - b2), got) == -1);
    }
    /* signature length not 64 */
    {
        unsigned char b2[64];
        unsigned char *p = b2;
        *p++ = 14;
        put32(p, 4 + 11 + 4 + 32); p += 4;
        put32(p, 11); p += 4; memcpy(p, "ssh-ed25519", 11); p += 11;
        put32(p, 32); p += 4; memset(p, 0, 32); p += 32; /* glen 32 != 64 */
        assert(nornd_agent_decode_sign_response(b2, (size_t)(p - b2), got) == -1);
    }
    /* signature length 64 but the bytes are truncated (glen > left) */
    {
        unsigned char b2[64];
        unsigned char *p = b2;
        *p++ = 14;
        put32(p, 4 + 11 + 4 + 10); p += 4; /* sigblob promises 10 sig bytes */
        put32(p, 11); p += 4; memcpy(p, "ssh-ed25519", 11); p += 11;
        put32(p, 64); p += 4; memset(p, 0, 10); p += 10; /* glen 64 > left 10 */
        assert(nornd_agent_decode_sign_response(b2, (size_t)(p - b2), got) == -1);
    }
}

/* ---- fake agent transport for sign_io ---- */
typedef struct {
    const unsigned char *frame; /* full framed response (4-byte len + body) */
    size_t framelen;
    size_t pos;
    int reads;
    int fail_write;
    int fail_read1;
    int fail_read2;
} fake_t;

static int fk_write(void *c, const unsigned char *b, size_t n) {
    fake_t *f = c;
    (void)b;
    (void)n;
    return f->fail_write ? -1 : 0;
}
static int fk_read(void *c, unsigned char *b, size_t n) {
    fake_t *f = c;
    f->reads++;
    if (f->reads == 1 && f->fail_read1) return -1;
    if (f->reads == 2 && f->fail_read2) return -1;
    if (f->pos + n > f->framelen) return -1;
    memcpy(b, f->frame + f->pos, n);
    f->pos += n;
    return 0;
}

static size_t frame_resp(const unsigned char sig[64], unsigned char *out) {
    unsigned char body[128];
    size_t blen = build_body(sig, body);
    put32(out, (uint32_t)blen);
    memcpy(out + 4, body, blen);
    return 4 + blen;
}

static void test_sign_io(void) {
    unsigned char pub[32];
    memset(pub, 0x11, sizeof(pub));
    unsigned char data[64];
    memset(data, 0x22, sizeof(data));
    unsigned char sig[64];
    memset(sig, 0x33, sizeof(sig));
    unsigned char out[64];

    unsigned char frame[160];
    size_t flen = frame_resp(sig, frame);

    /* null io / missing callbacks */
    assert(nornd_agent_sign_io(NULL, pub, data, 64, out) == -1);
    nornd_agent_io_t bad1 = {NULL, NULL, fk_read};
    assert(nornd_agent_sign_io(&bad1, pub, data, 64, out) == -1);
    nornd_agent_io_t bad2 = {NULL, fk_write, NULL};
    assert(nornd_agent_sign_io(&bad2, pub, data, 64, out) == -1);

    /* encode failure (oversized data) before any I/O */
    fake_t fe = {frame, flen, 0, 0, 0, 0, 0};
    nornd_agent_io_t ioe = {&fe, fk_write, fk_read};
    static unsigned char huge[NORND_AGENT_REQ_MAX + 16];
    assert(nornd_agent_sign_io(&ioe, pub, huge, sizeof(huge), out) == -1);

    /* write failure */
    fake_t fw = {frame, flen, 0, 0, 1, 0, 0};
    nornd_agent_io_t iow = {&fw, fk_write, fk_read};
    assert(nornd_agent_sign_io(&iow, pub, data, 64, out) == -1);

    /* first read (frame length) fails */
    fake_t fr1 = {frame, flen, 0, 0, 0, 1, 0};
    nornd_agent_io_t ior1 = {&fr1, fk_write, fk_read};
    assert(nornd_agent_sign_io(&ior1, pub, data, 64, out) == -1);

    /* frame length is zero */
    {
        unsigned char z[8];
        put32(z, 0);
        fake_t fz = {z, 4, 0, 0, 0, 0, 0};
        nornd_agent_io_t ioz = {&fz, fk_write, fk_read};
        assert(nornd_agent_sign_io(&ioz, pub, data, 64, out) == -1);
    }
    /* frame length exceeds the response ceiling */
    {
        unsigned char hb[4];
        put32(hb, NORND_AGENT_RESP_MAX + 1);
        fake_t fh = {hb, 4, 0, 0, 0, 0, 0};
        nornd_agent_io_t ioh = {&fh, fk_write, fk_read};
        assert(nornd_agent_sign_io(&ioh, pub, data, 64, out) == -1);
    }
    /* second read (body) fails */
    fake_t fr2 = {frame, flen, 0, 0, 0, 0, 1};
    nornd_agent_io_t ior2 = {&fr2, fk_write, fk_read};
    assert(nornd_agent_sign_io(&ior2, pub, data, 64, out) == -1);

    /* malformed body propagates a decode failure */
    {
        unsigned char bad[160];
        memcpy(bad, frame, flen);
        bad[4] = 0; /* corrupt the response type byte */
        fake_t fb = {bad, flen, 0, 0, 0, 0, 0};
        nornd_agent_io_t iob = {&fb, fk_write, fk_read};
        assert(nornd_agent_sign_io(&iob, pub, data, 64, out) == -1);
    }

    /* success: the signature is returned verbatim */
    fake_t fok = {frame, flen, 0, 0, 0, 0, 0};
    nornd_agent_io_t iok = {&fok, fk_write, fk_read};
    assert(nornd_agent_sign_io(&iok, pub, data, 64, out) == 0);
    assert(memcmp(out, sig, 64) == 0);
}

int main(void) {
    test_encode();
    test_decode();
    test_sign_io();
    printf("all nornd agent tests passed\n");
    return 0;
}
