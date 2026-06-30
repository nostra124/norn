/* SPDX-License-Identifier: MIT */
/* Test BEP-44 encoding/decoding */
#include "bep44.h"
#include "sha1.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sodium.h>

static void test_target(void) {
    unsigned char pk[32] = {0};
    unsigned char target[20];
    
    bep44_target(pk, target);
    
    unsigned char target2[20];
    sha1(pk, 32, target2);
    assert(memcmp(target, target2, 20) == 0);
    
    printf("  test_target: OK\n");
}

static void test_target_for_pubkey(void) {
    unsigned char pk[32] = {0};
    unsigned char target[20];
    
    bep44_target_for_pubkey(target, pk);
    
    unsigned char target2[20];
    unsigned char buf[33] = {'k'};
    memcpy(buf + 1, pk, 32);
    sha1(buf, 33, target2);
    assert(memcmp(target, target2, 20) == 0);
    
    printf("  test_target_for_pubkey: OK\n");
}

static void test_target_salted(void) {
    unsigned char pk[32] = {0};
    unsigned char salt[64] = {1,2,3,4,5};
    unsigned char target[20];
    
    bep44_target_salted(pk, salt, 5, target);
    
    unsigned char target2[20];
    unsigned char buf[96];
    memcpy(buf, pk, 32);
    memcpy(buf + 32, salt, 5);
    sha1(buf, 37, target2);
    assert(memcmp(target, target2, 20) == 0);
    
    unsigned char long_salt[128];
    memset(long_salt, 1, sizeof(long_salt));
    bep44_target_salted(pk, long_salt, sizeof(long_salt), target);
    
    printf("  test_target_salted: OK\n");
}

static void test_immutable_target(void) {
    unsigned char v[] = "test value";
    unsigned char target[20];
    
    int ret = bep44_immutable_target(v, sizeof(v) - 1, target);
    assert(ret == 0);
    
    unsigned char big[1001];
    ret = bep44_immutable_target(big, sizeof(big), target);
    assert(ret == -1);
    
    printf("  test_immutable_target: OK\n");
}

static void test_signbuf(void) {
    unsigned char value[] = "test value";
    unsigned char buf[256];
    
    int ret = bep44_signbuf(1, NULL, 0, buf, sizeof(buf));
    assert(ret >= 0);
    
    ret = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    assert(ret > 0);
    
    ret = bep44_signbuf(1, value, sizeof(value) - 1, buf, 5);
    assert(ret == -1);
    
    printf("  test_signbuf: OK\n");
}

static void test_signbuf_salted(void) {
    unsigned char value[] = "test value";
    unsigned char salt[] = "salt";
    unsigned char buf[256];
    
    int ret = bep44_signbuf_salted(NULL, 0, 1, value, sizeof(value) - 1, buf, sizeof(buf));
    assert(ret > 0);
    
    ret = bep44_signbuf_salted(salt, sizeof(salt) - 1, 1, value, sizeof(value) - 1, buf, sizeof(buf));
    assert(ret > 0);
    
    unsigned char long_salt[128];
    memset(long_salt, 1, sizeof(long_salt));
    ret = bep44_signbuf_salted(long_salt, sizeof(long_salt), 1, value, sizeof(value) - 1, buf, sizeof(buf));
    assert(ret > 0);
    
    ret = bep44_signbuf_salted(salt, sizeof(salt) - 1, 1, value, sizeof(value) - 1, buf, 5);
    assert(ret == -1);
    
    printf("  test_signbuf_salted: OK\n");
}

static void test_record_encode_decode(void) {
    bep44_record_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.version, "0.1.0");
    r.ip = 0x01020304;
    r.port = 6881;
    r.caps = 0x12345678;
    memcpy(r.host_pubkey, (unsigned char[32]){1,2,3,4}, 32);
    memcpy(r.node_id, (unsigned char[20]){5,6,7,8}, 20);
    r.route_ip = 0x0A0B0C0D;
    r.route_port = 8080;
    
    unsigned char out[256];
    
    int len = bep44_record_encode(&r, out, sizeof(out));
    assert(len > 0);
    
    bep44_record_t r2;
    int ret = bep44_record_decode(out, len, &r2);
    assert(ret == 0);
    assert(strcmp(r2.version, "0.1.0") == 0);
    assert(r2.ip == r.ip);
    assert(r2.port == r.port);
    assert(r2.caps == r.caps);
    assert(memcmp(r2.host_pubkey, r.host_pubkey, 32) == 0);
    assert(memcmp(r2.node_id, r.node_id, 20) == 0);
    assert(r2.route_ip == r.route_ip);
    assert(r2.route_port == r.route_port);
    
    printf("  test_record_encode_decode: OK\n");
}

static void test_record_with_services(void) {
    bep44_record_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.version, "0.1.0");
    r.nsvc = 2;
    r.svc_len = 10;
    memcpy(r.svc, (unsigned char[10]){1,2,3,4,5,6,7,8,9,10}, 10);
    
    unsigned char out[256];
    int len = bep44_record_encode(&r, out, sizeof(out));
    assert(len > 0);
    
    bep44_record_t r2;
    int ret = bep44_record_decode(out, len, &r2);
    assert(ret == 0);
    assert(r2.nsvc == 2);
    assert(r2.svc_len == 10);
    
    printf("  test_record_with_services: OK\n");
}

static void test_encode_decode(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "hello world";
    unsigned char out[1024];
    uint32_t seq = 1;
    
    int len = bep44_encode(out, sizeof(out), pk, value, sizeof(value) - 1, seq, sk);
    assert(len > 0);
    
    unsigned char pk2[32], sig[64];
    unsigned char *vout;
    size_t vlen;
    uint32_t seq2;
    
    int ret = bep44_decode(out, len, pk2, &vout, &vlen, &seq2, sig);
    assert(ret == 0);
    assert(memcmp(pk2, pk, 32) == 0);
    assert(vlen == sizeof(value) - 1);
    assert(memcmp(vout, value, vlen) == 0);
    assert(seq2 == seq);
    
    ret = bep44_decode(out, len, NULL, &vout, &vlen, &seq2, sig);
    assert(ret == 0);
    
    ret = bep44_decode(out, len, pk2, NULL, &vlen, &seq2, sig);
    assert(ret == 0);
    
    ret = bep44_decode(out, len, pk2, &vout, NULL, &seq2, sig);
    assert(ret == 0);
    
    ret = bep44_decode(out, len, pk2, &vout, &vlen, NULL, sig);
    assert(ret == 0);
    
    ret = bep44_decode(out, len, pk2, &vout, &vlen, &seq2, NULL);
    assert(ret == 0);
    
    printf("  test_encode_decode: OK\n");
}

static void test_encode_null(void) {
    unsigned char pk[32], sk[64], out[1024], value[] = "test";
    crypto_sign_keypair(pk, sk);
    
    assert(bep44_encode(NULL, sizeof(out), pk, value, 4, 1, sk) == -1);
    assert(bep44_encode(out, sizeof(out), NULL, value, 4, 1, sk) == -1);
    assert(bep44_encode(out, sizeof(out), pk, NULL, 4, 1, sk) == -1);
    assert(bep44_encode(out, sizeof(out), pk, value, 4, 1, NULL) == -1);
    
    printf("  test_encode_null: OK\n");
}

static void test_encode_overflow(void) {
    unsigned char pk[32], sk[64], out[1024];
    crypto_sign_keypair(pk, sk);
    
    unsigned char big[1001];
    memset(big, 'X', sizeof(big));
    assert(bep44_encode(out, sizeof(out), pk, big, sizeof(big), 1, sk) == -1);
    
    unsigned char small[10];
    int len = bep44_encode(out, 50, pk, small, sizeof(small), 1, sk);
    assert(len == -1);
    
    printf("  test_encode_overflow: OK\n");
}

static void test_decode_truncated(void) {
    unsigned char pk[32], sk[64], out[1024];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "test";
    int len = bep44_encode(out, sizeof(out), pk, value, 4, 1, sk);
    assert(len > 0);
    
    unsigned char pk2[32], sig[64];
    unsigned char *vout;
    size_t vlen;
    uint32_t seq2;
    
    assert(bep44_decode(NULL, len, pk2, &vout, &vlen, &seq2, sig) == -1);
    
    assert(bep44_decode(out, 10, pk2, &vout, &vlen, &seq2, sig) == -1);
    
    printf("  test_decode_truncated: OK\n");
}

static void test_decode_bad_sig(void) {
    unsigned char pk[32], sk[64], out[1024];
    crypto_sign_keypair(pk, sk);
    
    unsigned char value[] = "test";
    int len = bep44_encode(out, sizeof(out), pk, value, 4, 1, sk);
    assert(len > 0);
    
    unsigned char pk2[32], sig[64];
    unsigned char *vout;
    size_t vlen;
    uint32_t seq2;
    
    out[len - 1] ^= 0xFF;
    int ret = bep44_decode(out, len, pk2, &vout, &vlen, &seq2, sig);
    assert(ret == -1);
    
    printf("  test_decode_bad_sig: OK\n");
}

static void test_record_decode_invalid(void) {
    unsigned char buf[256];
    bep44_record_t r;
    
    assert(bep44_record_decode(NULL, 10, &r) == -1);
    
    assert(bep44_record_decode(buf, 0, &r) == -1);
    
    buf[0] = 255;
    assert(bep44_record_decode(buf, 10, &r) == -1);
    
    printf("  test_record_decode_invalid: OK\n");
}

static void test_sha1_padding_overflow(void) {
    /* 56-byte message: after 0x80 the remaining length is < 8 bytes, so padding
     * overflows into a second block (sha1.c line 43: rem > 56 true arm). */
    unsigned char msg[56];
    memset(msg, 'A', sizeof(msg));
    unsigned char out[20];
    sha1(msg, sizeof(msg), out);

    /* echo -n AAAA...(56) | sha1sum */
    static const unsigned char expect[20] = {
        0x6b,0x45,0xe3,0xcf,0x1e,0xb3,0x32,0x4b,0x9f,0xd4,
        0xdf,0x3b,0x83,0xd8,0x9c,0x4c,0x2c,0x4c,0xa8,0x96
    };
    assert(memcmp(out, expect, 20) == 0);

    printf("  test_sha1_padding_overflow: OK\n");
}

static void test_target_salted_nosalt(void) {
    /* salt NULL / saltlen 0: exercises bep44.c line 26 (salt && saltlen) false arm. */
    unsigned char pk[32] = {0};
    unsigned char target[20], target2[20];

    bep44_target_salted(pk, NULL, 0, target);
    sha1(pk, 32, target2);
    assert(memcmp(target, target2, 20) == 0);

    /* non-NULL salt but zero length still skips the copy. */
    unsigned char salt[4] = {9,9,9,9};
    bep44_target_salted(pk, salt, 0, target);
    assert(memcmp(target, target2, 20) == 0);

    printf("  test_target_salted_nosalt: OK\n");
}

static void test_signbuf_salted_branches(void) {
    /* salt non-NULL but saltlen 0: exercises bep44.c line 49 (salt && saltlen)
     * where the first operand is true but the second is false. */
    unsigned char value[] = "v";
    unsigned char salt[] = "salt";
    unsigned char buf[256];

    int ret = bep44_signbuf_salted(salt, 0, 1, value, sizeof(value) - 1, buf, sizeof(buf));
    assert(ret > 0);

    /* salt present with an outcap too small for even the salt block:
     * line 52 overflow (the || second operand) true. */
    ret = bep44_signbuf_salted(salt, sizeof(salt) - 1, 1, value, sizeof(value) - 1, buf, 3);
    assert(ret == -1);

    printf("  test_signbuf_salted_branches: OK\n");
}

static void test_record_encode_version_overflow(void) {
    /* version longer than 23 chars: line 70 (vl > 23) true arm. ip = 0 supplies a
     * NUL terminator immediately after the 24 'A's so strlen stays in bounds. */
    bep44_record_t r;
    memset(&r, 0, sizeof(r));
    memset(r.version, 'A', sizeof(r.version)); /* 24 bytes, no NUL */
    r.ip = 0;                                  /* terminator for strlen */

    unsigned char out[256];
    int len = bep44_record_encode(&r, out, sizeof(out));
    assert(len > 0);

    bep44_record_t r2;
    assert(bep44_record_decode(out, len, &r2) == 0);
    assert(strlen(r2.version) == 23); /* truncated to 23 */

    printf("  test_record_encode_version_overflow: OK\n");
}

static void test_record_encode_svc_branches(void) {
    /* have_svc = (nsvc > 0 && svc_len > 0 && svc_len <= MAX). Cover the
     * remaining && arms (line 72): nsvc==0, nsvc>0/svc_len==0, svc_len>MAX. */
    bep44_record_t r;
    unsigned char out[512];

    /* nsvc == 0 -> have_svc false (first operand false). */
    memset(&r, 0, sizeof(r));
    strcpy(r.version, "v");
    r.nsvc = 0; r.svc_len = 10;
    assert(bep44_record_encode(&r, out, sizeof(out)) > 0);

    /* nsvc > 0 but svc_len == 0 -> second operand false. */
    memset(&r, 0, sizeof(r));
    strcpy(r.version, "v");
    r.nsvc = 1; r.svc_len = 0;
    assert(bep44_record_encode(&r, out, sizeof(out)) > 0);

    /* svc_len > MAX -> third operand false. */
    memset(&r, 0, sizeof(r));
    strcpy(r.version, "v");
    r.nsvc = 1; r.svc_len = BEP44_REC_SVC_MAX + 1;
    assert(bep44_record_encode(&r, out, sizeof(out)) > 0);

    printf("  test_record_encode_svc_branches: OK\n");
}

static void test_record_encode_overflow(void) {
    /* need > outcap: line 74 true arm. */
    bep44_record_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.version, "0.1.0");

    unsigned char out[16];
    assert(bep44_record_encode(&r, out, sizeof(out)) == -1);

    printf("  test_record_encode_overflow: OK\n");
}

static void test_record_decode_short_body(void) {
    /* vl valid but body shorter than fixed fields: line 100 second (||) arm. */
    unsigned char buf[8];
    memset(buf, 0, sizeof(buf));
    buf[0] = 1; /* vl = 1, but len too small for the fixed trailer */
    bep44_record_t r;
    assert(bep44_record_decode(buf, sizeof(buf), &r) == -1);

    printf("  test_record_decode_short_body: OK\n");
}

static void test_record_decode_minimal(void) {
    /* A record exactly up to host_pubkey: no node_id (line 108 false), no
     * route-hint (line 110 false), no services (line 116 false). */
    bep44_record_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.version, "v");

    /* Build the minimal fixed body: vl(1)+ver+ip(4)+port(2)+ula(16)+caps(4)+pk(32). */
    size_t vl = strlen(r.version);
    size_t minlen = 1 + vl + 4 + 2 + 16 + 4 + 32;
    unsigned char out[256];
    int len = bep44_record_encode(&r, out, sizeof(out));
    assert(len > 0);

    bep44_record_t r2;
    assert(bep44_record_decode(out, minlen, &r2) == 0);
    /* node_id, route-hint and services stay zeroed. */
    unsigned char zero20[20] = {0};
    assert(memcmp(r2.node_id, zero20, 20) == 0);
    assert(r2.route_ip == 0 && r2.route_port == 0);
    assert(r2.nsvc == 0 && r2.svc_len == 0);

    printf("  test_record_decode_minimal: OK\n");
}

static void test_record_decode_svc_clamp(void) {
    /* svc blob larger than BEP44_REC_SVC_MAX in the wire input: line 119 true arm
     * (remain > MAX -> clamp). Build a record body manually with an oversized
     * trailing svc blob. */
    bep44_record_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.version, "v");
    r.nsvc = 5;
    r.svc_len = BEP44_REC_SVC_MAX; /* max valid blob */
    memset(r.svc, 0xAB, BEP44_REC_SVC_MAX);

    unsigned char out[512];
    int len = bep44_record_encode(&r, out, sizeof(out));
    assert(len > 0);

    /* Append extra bytes so the decoder sees remain > MAX and clamps. */
    memset(out + len, 0xCD, 50);
    bep44_record_t r2;
    assert(bep44_record_decode(out, (size_t)len + 50, &r2) == 0);
    assert(r2.svc_len == BEP44_REC_SVC_MAX);

    printf("  test_record_decode_svc_clamp: OK\n");
}

static void test_decode_value_overruns_len(void) {
    /* vlen field claims more bytes than present: line 172 true arm
     * (o + vl + 64 > len). */
    unsigned char pk[32], sk[64], out[1024];
    crypto_sign_keypair(pk, sk);
    unsigned char value[] = "test";
    int len = bep44_encode(out, sizeof(out), pk, value, 4, 1, sk);
    assert(len > 0);

    /* Bump the BE vlen field (offset 36) to a huge value. */
    out[36] = 0xFF; out[37] = 0xFF;
    unsigned char pk2[32], sig[64];
    unsigned char *vout; size_t vlen; uint32_t seq2;
    assert(bep44_decode(out, len, pk2, &vout, &vlen, &seq2, sig) == -1);

    printf("  test_decode_value_overruns_len: OK\n");
}

static void test_decode_signbuf_overflow(void) {
    /* vlen large enough that the canonical sign buffer (2048B) overflows:
     * line 181 true arm. The fixed header is 38 bytes, value is vl bytes, sig 64.
     * Pick vl so that o + vl + 64 <= len but signbuf overflows. */
    size_t vl = 2048;
    size_t len = 32 + 4 + 2 + vl + 64;
    unsigned char *in = calloc(1, len);
    assert(in != NULL);
    /* vlen field (BE) at offset 36..37 */
    in[36] = (unsigned char)((vl >> 8) & 0xFF);
    in[37] = (unsigned char)(vl & 0xFF);

    unsigned char pk2[32], sig[64];
    unsigned char *vout; size_t vlen; uint32_t seq2;
    assert(bep44_decode(in, len, pk2, &vout, &vlen, &seq2, sig) == -1);

    free(in);
    printf("  test_decode_signbuf_overflow: OK\n");
}

int main(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }
    
    printf("test_bep44:\n");
    
    test_target();
    test_target_for_pubkey();
    test_target_salted();
    test_immutable_target();
    test_signbuf();
    test_signbuf_salted();
    test_record_encode_decode();
    test_record_with_services();
    test_encode_decode();
    test_encode_null();
    test_encode_overflow();
    test_decode_truncated();
    test_decode_bad_sig();
    test_record_decode_invalid();
    test_sha1_padding_overflow();
    test_target_salted_nosalt();
    test_signbuf_salted_branches();
    test_record_encode_version_overflow();
    test_record_encode_svc_branches();
    test_record_encode_overflow();
    test_record_decode_short_body();
    test_record_decode_minimal();
    test_record_decode_svc_clamp();
    test_decode_value_overruns_len();
    test_decode_signbuf_overflow();

    printf("test_bep44: OK\n");
    return 0;
}