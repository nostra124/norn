/* Test BEP-44 encoding/decoding */
#include "bep44.h"
#include "sha1.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
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
    
    printf("test_bep44: OK\n");
    return 0;
}