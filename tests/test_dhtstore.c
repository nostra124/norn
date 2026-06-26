/* Test DHT storage with 100% coverage */
#include "dhtstore.h"
#include "bep44.h"
#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>

static void test_init(void) {
    size_t budget = dhtstore_init(2, 0);
    assert(budget == 2 * 1024 * 1024);
    assert(dhtstore_count() == 0);
    assert(dhtstore_bytes() == 0);
    printf("  test_init: OK\n");
}

static void test_client_only(void) {
    size_t budget = dhtstore_init(2, 1);
    assert(budget == 0);
    
    unsigned char value[] = "test";
    unsigned char target[20];
    assert(dhtstore_put_immutable(value, sizeof(value) - 1, 0x01020304, target) == 0);
    assert(dhtstore_count() == 0);
    printf("  test_client_only: OK\n");
}

static void test_put_immutable_basic(void) {
    dhtstore_init(2, 0);
    
    unsigned char value[] = "hello world";
    unsigned char target[20];
    
    assert(dhtstore_put_immutable(value, sizeof(value) - 1, 0x01020304, target) == 1);
    assert(dhtstore_count() == 1);
    assert(dhtstore_bytes() == sizeof(value) - 1);
    
    unsigned char k_out[32], sig_out[64], v_out[1024];
    uint32_t seq_out;
    size_t vlen_out;
    assert(dhtstore_get(target, k_out, &seq_out, v_out, sizeof(v_out), &vlen_out, sig_out) == 1);
    assert(vlen_out == sizeof(value) - 1);
    assert(memcmp(v_out, value, vlen_out) == 0);
    printf("  test_put_immutable_basic: OK\n");
}

static void test_put_immutable_null(void) {
    dhtstore_init(2, 0);
    
    unsigned char value[] = "test";
    unsigned char target[20];
    
    assert(dhtstore_put_immutable(NULL, sizeof(value), 0x01020304, target) == 0);
    assert(dhtstore_put_immutable(value, 0, 0x01020304, target) == 0);
    printf("  test_put_immutable_null: OK\n");
}

static void test_get_null(void) {
    dhtstore_init(2, 0);
    
    unsigned char value[] = "test";
    unsigned char target[20];
    dhtstore_put_immutable(value, sizeof(value) - 1, 0x01020304, target);
    
    unsigned char k_out[32], sig_out[64], v_out[1024];
    uint32_t seq_out;
    size_t vlen_out;
    unsigned char bad_target[20] = {0};
    
    assert(dhtstore_get(bad_target, k_out, &seq_out, v_out, sizeof(v_out), &vlen_out, sig_out) == 0);
    assert(dhtstore_get(target, k_out, &seq_out, v_out, sizeof(v_out), &vlen_out, sig_out) == 1);
    printf("  test_get_null: OK\n");
}

static void test_get_ex(void) {
    dhtstore_init(2, 0);
    
    unsigned char value[] = "test";
    unsigned char target[20];
    dhtstore_put_immutable(value, sizeof(value), 0x01020304, target);
    
    unsigned char k_out[32], sig_out[64], v_out[1024];
    uint32_t seq_out;
    size_t vlen_out;
    int immutable_out;
    
    assert(dhtstore_get_ex(target, k_out, &seq_out, v_out, sizeof(v_out), &vlen_out, sig_out, &immutable_out) == 1);
    assert(immutable_out == 1);
    assert(vlen_out == sizeof(value));
    printf("  test_get_ex: OK\n");
}

static void test_del(void) {
    dhtstore_init(2, 0);
    
    unsigned char value[] = "test";
    unsigned char target[20];
    dhtstore_put_immutable(value, sizeof(value), 0x01020304, target);
    
    assert(dhtstore_count() == 1);
    assert(dhtstore_del(target) == 1);
    assert(dhtstore_count() == 0);
    assert(dhtstore_del(target) == 0);
    
    unsigned char dummy[20] = {0};
    assert(dhtstore_del(NULL) == 0);
    assert(dhtstore_del(dummy) == 0);
    printf("  test_del: OK\n");
}

static void test_list(void) {
    dhtstore_init(2, 0);
    
    unsigned char v1[] = "value1";
    unsigned char v2[] = "value2";
    unsigned char t1[20], t2[20];
    
    dhtstore_put_immutable(v1, sizeof(v1), 0x01020304, t1);
    dhtstore_put_immutable(v2, sizeof(v2), 0x01020304, t2);
    
    dht_item_info_t items[10];
    int count = dhtstore_list(1, items, 10);
    assert(count == 2);
    
    count = dhtstore_list(0, items, 10);
    assert(count == 0);

    /* max smaller than held count -> loop stops on the n<max guard */
    count = dhtstore_list(1, items, 1);
    assert(count == 1);
    printf("  test_list: OK\n");
}

static void test_per_ip_cap(void) {
    dhtstore_init(1, 0);
    
    unsigned char value[] = "test";
    unsigned char target[20];
    uint32_t same_ip = 0x01020304;
    
    for (int i = 0; i < DHTSTORE_PER_IP; i++) {
        unsigned char v[8];
        memcpy(v, value, 4);
        v[0] = (unsigned char)i;
        assert(dhtstore_put_immutable(v, 4, same_ip, target) == 1);
    }
    
    unsigned char extra[] = "extra";
    assert(dhtstore_put_immutable(extra, 5, same_ip, target) == 0);
    printf("  test_per_ip_cap: OK\n");
}

static void test_seq_monotonicity(void) {
    dhtstore_init(2, 0);
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    unsigned char target[20];
    bep44_target(kp.public_key, target);
    
    unsigned char v1[] = "value1";
    unsigned char v2[] = "value2";
    unsigned char v3[] = "value3";
    unsigned char buf[256];
    unsigned char sig1[64], sig2[64], sig3[64];
    
    int len1 = bep44_signbuf(1, v1, sizeof(v1) - 1, buf, sizeof(buf));
    bf_sign(sig1, buf, (size_t)len1, kp.secret_key);
    
    int len2 = bep44_signbuf(2, v2, sizeof(v2) - 1, buf, sizeof(buf));
    bf_sign(sig2, buf, (size_t)len2, kp.secret_key);
    
    int len3 = bep44_signbuf(3, v3, sizeof(v3) - 1, buf, sizeof(buf));
    bf_sign(sig3, buf, (size_t)len3, kp.secret_key);
    
    assert(dhtstore_put(target, kp.public_key, 1, v1, sizeof(v1) - 1, sig1, NULL, 0, 0x01020304) == 1);
    assert(dhtstore_count() == 1);
    
    assert(dhtstore_put(target, kp.public_key, 1, v1, sizeof(v1) - 1, sig1, NULL, 0, 0x01020304) == 0);
    
    assert(dhtstore_put(target, kp.public_key, 0, v1, sizeof(v1) - 1, sig1, NULL, 0, 0x01020304) == 0);
    
    assert(dhtstore_put(target, kp.public_key, 2, v2, sizeof(v2) - 1, sig2, NULL, 0, 0x01020304) == 1);
    assert(dhtstore_count() == 1);
    
    assert(dhtstore_put(target, kp.public_key, 3, v3, sizeof(v3) - 1, sig3, NULL, 0, 0x01020304) == 1);
    printf("  test_seq_monotonicity: OK\n");
}

static void test_budget_eviction(void) {
    dhtstore_init(1, 0);
    
    size_t budget = 1 * 1024 * 1024;
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    unsigned char target[20];
    bep44_target(kp.public_key, target);
    
    unsigned char value[] = "test value for eviction";
    unsigned char buf[256];
    unsigned char sig[64];
    int len = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    bf_sign(sig, buf, (size_t)len, kp.secret_key);
    
    for (int i = 0; i < 1000; i++) {
        keypair_t kp2;
        crypto_keypair_new(&kp2);
        unsigned char t[20];
        bep44_target(kp2.public_key, t);
        unsigned char s[64];
        unsigned char b[256];
        int l = bep44_signbuf(1, value, sizeof(value) - 1, b, sizeof(b));
        bf_sign(s, b, (size_t)l, kp2.secret_key);
        dhtstore_put(t, kp2.public_key, 1, value, sizeof(value) - 1, s, NULL, 0, (uint32_t)(0x01020304 + i));
    }
    
    assert(dhtstore_bytes() <= budget);
    printf("  test_budget_eviction: OK\n");
}

static void test_lru_eviction(void) {
    size_t budget = dhtstore_init(1, 0);
    assert(budget == 1 * 1024 * 1024);
    
    int added = 0;
    for (int i = 0; i < 1200; i++) {
        keypair_t kp2;
        crypto_keypair_new(&kp2);
        unsigned char t[20];
        bep44_target(kp2.public_key, t);
        
        unsigned char value[900];
        memset(value, 'X' + (i % 26), sizeof(value));
        unsigned char b[3000];
        int l = bep44_signbuf(1, value, sizeof(value), b, sizeof(b));
        unsigned char s[64];
        bf_sign(s, b, (size_t)l, kp2.secret_key);
        
        if (dhtstore_put(t, kp2.public_key, 1, value, sizeof(value), s, NULL, 0, (uint32_t)(0x01020304 + i)) == 1)
            added++;
    }
    
    printf("    Added %d items, count=%d, bytes=%zu, budget=%zu\n", added, dhtstore_count(), dhtstore_bytes(), budget);
    assert(added > 0);
    assert(dhtstore_bytes() <= budget);
    printf("  test_lru_eviction: OK\n");
}

static void test_salted_item(void) {
    dhtstore_init(2, 0);
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    unsigned char salt[] = "mysalt";
    unsigned char target[20];
    bep44_target_salted(kp.public_key, salt, sizeof(salt) - 1, target);
    
    unsigned char value[] = "salted value";
    unsigned char buf[256];
    int len = bep44_signbuf_salted(salt, sizeof(salt) - 1, 1, value, sizeof(value) - 1, buf, sizeof(buf));
    unsigned char sig[64];
    bf_sign(sig, buf, (size_t)len, kp.secret_key);
    
    assert(dhtstore_put(target, kp.public_key, 1, value, sizeof(value) - 1, sig, salt, sizeof(salt) - 1, 0x01020304) == 1);
    
    unsigned char k_out[32], v_out[1024], sig_out[64];
    uint32_t seq_out;
    size_t vlen_out;
    assert(dhtstore_get(target, k_out, &seq_out, v_out, sizeof(v_out), &vlen_out, sig_out) == 1);
    assert(memcmp(v_out, value, vlen_out) == 0);
    printf("  test_salted_item: OK\n");
}

static void test_invalid_signature(void) {
    dhtstore_init(2, 0);
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    unsigned char target[20];
    bep44_target(kp.public_key, target);
    
    unsigned char value[] = "value";
    unsigned char wrong_sig[64] = {0};
    
    assert(dhtstore_put(target, kp.public_key, 1, value, sizeof(value) - 1, wrong_sig, NULL, 0, 0x01020304) == 0);
    assert(dhtstore_count() == 0);
    printf("  test_invalid_signature: OK\n");
}

static void test_wrong_target(void) {
    dhtstore_init(2, 0);
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    unsigned char target[20];
    bep44_target(kp.public_key, target);
    
    unsigned char wrong_target[20];
    memset(wrong_target, 0xAA, 20);
    
    unsigned char value[] = "value";
    unsigned char buf[256];
    unsigned char sig[64];
    int len = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    bf_sign(sig, buf, (size_t)len, kp.secret_key);
    
    assert(dhtstore_put(wrong_target, kp.public_key, 1, value, sizeof(value) - 1, sig, NULL, 0, 0x01020304) == 0);
    printf("  test_wrong_target: OK\n");
}

static void test_immutable_dedup(void) {
    dhtstore_init(2, 0);
    
    unsigned char value[] = "same value";
    unsigned char t1[20], t2[20];
    
    assert(dhtstore_put_immutable(value, sizeof(value) - 1, 0x01020304, t1) == 1);
    assert(dhtstore_put_immutable(value, sizeof(value) - 1, 0x05060708, t2) == 1);
    
    assert(memcmp(t1, t2, 20) == 0);
    assert(dhtstore_count() == 1);
    printf("  test_immutable_dedup: OK\n");
}

static void test_get_ex_buffer_truncation(void) {
    dhtstore_init(2, 0);
    
    unsigned char value[] = "this is a longer value";
    unsigned char target[20];
    dhtstore_put_immutable(value, sizeof(value) - 1, 0x01020304, target);
    
    unsigned char k_out[32], sig_out[64], v_out[8];
    uint32_t seq_out;
    size_t vlen_out;
    int immutable_out;
    
    assert(dhtstore_get_ex(target, k_out, &seq_out, v_out, sizeof(v_out), &vlen_out, sig_out, &immutable_out) == 1);
    assert(vlen_out == sizeof(v_out));
    assert(memcmp(v_out, value, sizeof(v_out)) == 0);
    printf("  test_get_ex_buffer_truncation: OK\n");
}

static void test_large_value_reject(void) {
    dhtstore_init(2, 0);
    
    unsigned char large[DHTSTORE_VMAX + 100];
    memset(large, 'X', sizeof(large));
    unsigned char target[20];
    
    assert(dhtstore_put_immutable(large, sizeof(large), 0x01020304, target) == 0);
    printf("  test_large_value_reject: OK\n");
}

static void test_auto_budget(void) {
    size_t budget = dhtstore_init(0, 0);
    assert(budget >= 2 * 1024 * 1024);
    assert(budget <= 64 * 1024 * 1024);
    printf("  test_auto_budget: OK\n");
}

/* dhtstore_put rejects everything while serving is disabled (budget==0). */
static void test_put_client_only(void) {
    dhtstore_init(2, 1);

    keypair_t kp;
    crypto_keypair_new(&kp);
    unsigned char target[20];
    bep44_target(kp.public_key, target);

    unsigned char value[] = "v";
    unsigned char buf[256], sig[64];
    int len = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    bf_sign(sig, buf, (size_t)len, kp.secret_key);

    assert(dhtstore_put(target, kp.public_key, 1, value, sizeof(value) - 1, sig, NULL, 0, 0x01020304) == 0);
    printf("  test_put_client_only: OK\n");
}

/* Each NULL/invalid-argument arm of the dhtstore_put guard. */
static void test_put_invalid_args(void) {
    dhtstore_init(2, 0);

    keypair_t kp;
    crypto_keypair_new(&kp);
    unsigned char target[20];
    bep44_target(kp.public_key, target);

    unsigned char value[] = "v";
    unsigned char buf[256], sig[64];
    int len = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
    bf_sign(sig, buf, (size_t)len, kp.secret_key);

    size_t vlen = sizeof(value) - 1;
    assert(dhtstore_put(NULL, kp.public_key, 1, value, vlen, sig, NULL, 0, 1) == 0);
    assert(dhtstore_put(target, NULL, 1, value, vlen, sig, NULL, 0, 1) == 0);
    assert(dhtstore_put(target, kp.public_key, 1, NULL, vlen, sig, NULL, 0, 1) == 0);
    assert(dhtstore_put(target, kp.public_key, 1, value, 0, sig, NULL, 0, 1) == 0);
    assert(dhtstore_put(target, kp.public_key, 1, value, DHTSTORE_VMAX + 1, sig, NULL, 0, 1) == 0);
    assert(dhtstore_put(target, kp.public_key, 1, value, vlen, NULL, NULL, 0, 1) == 0);
    assert(dhtstore_count() == 0);
    printf("  test_put_invalid_args: OK\n");
}

/* salt!=NULL but saltlen==0 takes the unsalted path (false arm of `salt && saltlen`). */
static void test_put_salt_zero_len(void) {
    dhtstore_init(2, 0);

    keypair_t kp;
    crypto_keypair_new(&kp);
    unsigned char target[20];
    bep44_target(kp.public_key, target);   /* unsalted target */

    unsigned char salt[] = "ignored";
    unsigned char value[] = "value";
    unsigned char buf[256], sig[64];
    /* sign with the unsalted buffer; saltlen==0 must hash/verify as unsalted */
    int len = bep44_signbuf_salted(salt, 0, 1, value, sizeof(value) - 1, buf, sizeof(buf));
    bf_sign(sig, buf, (size_t)len, kp.secret_key);

    assert(dhtstore_put(target, kp.public_key, 1, value, sizeof(value) - 1, sig, salt, 0, 0x01020304) == 1);
    printf("  test_put_salt_zero_len: OK\n");
}

/* put_immutable with target_out==NULL (false arm of `if (target_out)`). */
static void test_put_immutable_null_out(void) {
    dhtstore_init(2, 0);
    unsigned char value[] = "no-out";
    assert(dhtstore_put_immutable(value, sizeof(value) - 1, 0x01020304, NULL) == 1);
    assert(dhtstore_count() == 1);
    printf("  test_put_immutable_null_out: OK\n");
}

/* get_ex with every optional output pointer NULL, plus the v_out/vlen_out
 * partial-NULL combinations (both arms of `v_out && vlen_out`). */
static void test_get_ex_null_outputs(void) {
    dhtstore_init(2, 0);
    unsigned char value[] = "payload";
    unsigned char target[20];
    dhtstore_put_immutable(value, sizeof(value) - 1, 0x01020304, target);

    /* all optional outputs NULL */
    assert(dhtstore_get_ex(target, NULL, NULL, NULL, 0, NULL, NULL, NULL) == 1);

    unsigned char v_out[64];
    size_t vlen_out = 123;
    /* v_out set but vlen_out NULL -> value not copied */
    assert(dhtstore_get_ex(target, NULL, NULL, v_out, sizeof(v_out), NULL, NULL, NULL) == 1);
    /* v_out NULL but vlen_out set -> value not copied, vlen_out untouched */
    assert(dhtstore_get_ex(target, NULL, NULL, NULL, 0, &vlen_out, NULL, NULL) == 1);
    assert(vlen_out == 123);
    printf("  test_get_ex_null_outputs: OK\n");
}

/* LRU picks the strictly-oldest item: store one, wait a second so a later
 * batch has a larger `stored`, then force eviction (exercises the `<` true arm). */
static void test_lru_oldest_selected(void) {
    dhtstore_init(1, 0);
    size_t budget = 1 * 1024 * 1024;

    keypair_t kp0;
    crypto_keypair_new(&kp0);
    unsigned char t0[20];
    bep44_target(kp0.public_key, t0);
    unsigned char value[900];
    memset(value, 'A', sizeof(value));
    unsigned char b0[3000], s0[64];
    int l0 = bep44_signbuf(1, value, sizeof(value), b0, sizeof(b0));
    bf_sign(s0, b0, (size_t)l0, kp0.secret_key);
    assert(dhtstore_put(t0, kp0.public_key, 1, value, sizeof(value), s0, NULL, 0, 0x01020304) == 1);

    sleep(1);   /* t0 is the oldest; the "mid" item below is strictly newer than t0 */

    /* A second item at a distinct middle timestamp. After t0 (at index 0) is
     * evicted and swap-replaced by a newest item, the next eviction scan finds
     * this older "mid" item at a higher index, exercising the `<` true arm. */
    keypair_t kpm;
    crypto_keypair_new(&kpm);
    unsigned char tm[20];
    bep44_target(kpm.public_key, tm);
    unsigned char bm[3000], sm[64];
    int lm = bep44_signbuf(1, value, sizeof(value), bm, sizeof(bm));
    bf_sign(sm, bm, (size_t)lm, kpm.secret_key);
    assert(dhtstore_put(tm, kpm.public_key, 1, value, sizeof(value), sm, NULL, 0, 0x01030305) == 1);

    sleep(1);   /* the bulk batch below is strictly newer than both t0 and tm */

    for (int i = 0; i < 1500; i++) {
        keypair_t kp2;
        crypto_keypair_new(&kp2);
        unsigned char t[20];
        bep44_target(kp2.public_key, t);
        unsigned char b[3000], s[64];
        int l = bep44_signbuf(1, value, sizeof(value), b, sizeof(b));
        bf_sign(s, b, (size_t)l, kp2.secret_key);
        dhtstore_put(t, kp2.public_key, 1, value, sizeof(value), s, NULL, 0, (uint32_t)(0x02000000 + i));
    }

    /* the original (oldest) item must have been evicted */
    unsigned char v_out[1024];
    size_t vlen_out;
    assert(dhtstore_get(t0, NULL, NULL, v_out, sizeof(v_out), &vlen_out, NULL) == 0);
    assert(dhtstore_bytes() <= budget);
    printf("  test_lru_oldest_selected: OK\n");
}

int main(void) {
    crypto_init();
    printf("test_dhtstore:\n");
    
    test_init();
    test_client_only();
    test_auto_budget();
    test_put_immutable_basic();
    test_put_immutable_null();
    test_get_null();
    test_get_ex();
    test_del();
    test_list();
    test_per_ip_cap();
    test_seq_monotonicity();
    test_budget_eviction();
    test_lru_eviction();
    test_salted_item();
    test_invalid_signature();
    test_wrong_target();
    test_immutable_dedup();
    test_get_ex_buffer_truncation();
    test_large_value_reject();
    test_put_client_only();
    test_put_invalid_args();
    test_put_salt_zero_len();
    test_put_immutable_null_out();
    test_get_ex_null_outputs();
    test_lru_oldest_selected();

    printf("test_dhtstore: OK\n");
    return 0;
}