/* SPDX-License-Identifier: MIT */
#include "bencode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

static void test_encode_int(void) {
    bencode_value_t *val = bencode_int_new(42);
    assert(val != NULL);
    assert(val->type == BENCODE_INT);
    assert(val->val.int_val == 42);
    
    size_t len;
    char *encoded = bencode_encode(val, &len);
    assert(encoded != NULL);
    assert(len == 4);
    assert(memcmp(encoded, "i42e", 4) == 0);
    
    free(encoded);
    bencode_free(val);
    printf("  test_encode_int: OK\n");
}

static void test_encode_int_negative(void) {
    bencode_value_t *val = bencode_int_new(-123);
    assert(val != NULL);
    
    size_t len;
    char *encoded = bencode_encode(val, &len);
    assert(encoded != NULL);
    assert(len == 6);
    assert(memcmp(encoded, "i-123e", 6) == 0);
    
    free(encoded);
    bencode_free(val);
    printf("  test_encode_int_negative: OK\n");
}

static void test_encode_string(void) {
    bencode_value_t *val = bencode_string_new("hello", 5);
    assert(val != NULL);
    assert(val->type == BENCODE_STRING);
    assert(val->val.str_val.len == 5);
    
    size_t len;
    char *encoded = bencode_encode(val, &len);
    assert(encoded != NULL);
    assert(len == 7);
    assert(memcmp(encoded, "5:hello", 7) == 0);
    
    free(encoded);
    bencode_free(val);
    printf("  test_encode_string: OK\n");
}

static void test_encode_list(void) {
    bencode_value_t *list = bencode_list_new();
    assert(list != NULL);
    
    bencode_value_t *s1 = bencode_string_new("spam", 4);
    bencode_value_t *s2 = bencode_string_new("eggs", 4);
    
    assert(bencode_list_add(list, s1) == 0);
    assert(bencode_list_add(list, s2) == 0);
    
    size_t len;
    char *encoded = bencode_encode(list, &len);
    assert(encoded != NULL);
    assert(len == 14);
    assert(memcmp(encoded, "l4:spam4:eggse", 14) == 0);
    
    free(encoded);
    bencode_free(list);
    printf("  test_encode_list: OK\n");
}

static void test_encode_dict(void) {
    bencode_value_t *dict = bencode_dict_new();
    assert(dict != NULL);
    
    bencode_value_t *key1 = bencode_string_new("cow", 3);
    bencode_value_t *val1 = bencode_int_new(42);
    
    assert(bencode_dict_add(dict, "cow", key1) == 0);
    assert(bencode_dict_add(dict, "milk", val1) == 0);
    
    size_t len;
    char *encoded = bencode_encode(dict, &len);
    assert(encoded != NULL);
    
    free(encoded);
    bencode_free(dict);
    printf("  test_encode_dict: OK\n");
}

static void test_decode_int(void) {
    const char *data = "i42e";
    size_t pos = 0;
    bencode_value_t *val = bencode_decode(data, 4, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_INT);
    assert(val->val.int_val == 42);
    assert(pos == 4);
    
    bencode_free(val);
    printf("  test_decode_int: OK\n");
}

static void test_decode_string(void) {
    const char *data = "5:hello";
    size_t pos = 0;
    bencode_value_t *val = bencode_decode(data, 7, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_STRING);
    assert(val->val.str_val.len == 5);
    assert(memcmp(val->val.str_val.data, "hello", 5) == 0);
    assert(pos == 7);
    
    bencode_free(val);
    printf("  test_decode_string: OK\n");
}

static void test_decode_list(void) {
    const char *data = "l4:spam4:eggse";
    size_t pos = 0;
    bencode_value_t *val = bencode_decode(data, 14, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_LIST);
    assert(val->val.list_val.count == 2);
    
    bencode_free(val);
    printf("  test_decode_list: OK\n");
}

static void test_decode_dict(void) {
    const char *data = "d3:cow3:moo4:spami42ee";
    size_t pos = 0;
    bencode_value_t *val = bencode_decode(data, 22, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_DICT);
    assert(val->val.dict_val.count == 2);
    
    bencode_value_t *cow = bencode_dict_get(val, "cow");
    assert(cow != NULL);
    assert(cow->type == BENCODE_STRING);
    
    bencode_free(val);
    printf("  test_decode_dict: OK\n");
}

static void test_decode_empty(void) {
    size_t pos = 0;
    bencode_value_t *val = bencode_decode("", 0, &pos);
    assert(val == NULL);
    
    val = bencode_decode(NULL, 0, &pos);
    assert(val == NULL);
    
    val = bencode_decode("i42e", 4, NULL);
    assert(val == NULL);
    
    printf("  test_decode_empty: OK\n");
}

static void test_decode_truncated(void) {
    const char *data = "i42";
    size_t pos = 0;
    bencode_value_t *val = bencode_decode(data, 3, &pos);
    assert(val == NULL);
    
    data = "5:hell";
    pos = 0;
    val = bencode_decode(data, 6, &pos);
    assert(val == NULL);
    
    printf("  test_decode_truncated: OK\n");
}

static void test_decode_malformed(void) {
    const char *data = "i99e";  /* Valid integer */
    size_t pos = 0;
    bencode_value_t *val = bencode_decode(data, 4, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_INT);
    assert(val->val.int_val == 99);
    bencode_free(val);
    
    data = "ix";  /* Missing 'e' terminator */
    pos = 0;
    val = bencode_decode(data, 2, &pos);
    assert(val == NULL);
    
    data = "le";  /* Empty list */
    pos = 0;
    val = bencode_decode(data, 2, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_LIST);
    assert(val->val.list_val.count == 0);
    bencode_free(val);
    
    data = "de";  /* Empty dict */
    pos = 0;
    val = bencode_decode(data, 2, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_DICT);
    assert(val->val.dict_val.count == 0);
    bencode_free(val);
    
    printf("  test_decode_malformed: OK\n");
}

static void test_decode_overflow(void) {
    const char *data = "i999999999999999999999999999e";
    size_t pos = 0;
    bencode_value_t *val = bencode_decode(data, strlen(data), &pos);
    assert(val == NULL);
    
    printf("  test_decode_overflow: OK\n");
}

static void test_decode_int_min(void) {
    const char *data = "i-9223372036854775808e";
    size_t pos = 0;
    bencode_value_t *val = bencode_decode(data, strlen(data), &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_INT);
    assert(val->val.int_val == INT64_MIN);
    bencode_free(val);
    
    printf("  test_decode_int_min: OK\n");
}

static void test_decode_depth(void) {
    char data[100];
    size_t len = 0;
    
    for (int i = 0; i < 35; i++) {
        data[len++] = 'l';
    }
    data[len++] = 'e';
    for (int i = 0; i < 35; i++) {
        data[len++] = 'e';
    }
    
    size_t pos = 0;
    bencode_value_t *val = bencode_decode(data, len, &pos);
    assert(val == NULL);
    
    printf("  test_decode_depth: OK\n");
}

static void test_dict_get(void) {
    const char *data = "d3:foo3:bare";
    size_t pos = 0;
    bencode_value_t *dict = bencode_decode(data, 12, &pos);
    assert(dict != NULL);
    
    bencode_value_t *foo = bencode_dict_get(dict, "foo");
    assert(foo != NULL);
    assert(foo->type == BENCODE_STRING);
    assert(foo->val.str_val.len == 3);
    
    bencode_value_t *missing = bencode_dict_get(dict, "missing");
    assert(missing == NULL);
    
    bencode_value_t *null_dict = bencode_dict_get(NULL, "foo");
    assert(null_dict == NULL);
    
    bencode_free(dict);
    printf("  test_dict_get: OK\n");
}

static void test_free_null(void) {
    bencode_free(NULL);
    printf("  test_free_null: OK\n");
}

static void test_encode_null(void) {
    size_t len;
    char *encoded = bencode_encode(NULL, &len);
    assert(encoded == NULL);
    printf("  test_encode_null: OK\n");
}

static void test_roundtrip(void) {
    const char *original = "l5:helloi42ed3:foo3:baree";
    size_t pos = 0;
    
    bencode_value_t *val1 = bencode_decode(original, strlen(original), &pos);
    assert(val1 != NULL);
    
    size_t len;
    char *encoded = bencode_encode(val1, &len);
    assert(encoded != NULL);
    
    pos = 0;
    bencode_value_t *val2 = bencode_decode(encoded, len, &pos);
    assert(val2 != NULL);
    
    free(encoded);
    bencode_free(val1);
    bencode_free(val2);
    printf("  test_roundtrip: OK\n");
}

/* Decode error/edge inputs: each must reject (return NULL) unless noted, exercising
 * the parse_int_bounded, decode_int, decode_string, decode_list and decode_dict
 * rejection branches reachable through the public bencode_decode entry point. */
static void test_decode_int_edges(void) {
    size_t pos;
    bencode_value_t *val;

    /* small negative integer: non-MIN branch of parse_int_bounded's ternary */
    pos = 0;
    val = bencode_decode("i-42e", 5, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_INT);
    assert(val->val.int_val == -42);
    assert(pos == 5);
    bencode_free(val);

    /* '-' with no following digit */
    pos = 0;
    assert(bencode_decode("i-e", 3, &pos) == NULL);

    /* negative overflow (INT64_MIN - 1) */
    pos = 0;
    assert(bencode_decode("i-9223372036854775809e", 22, &pos) == NULL);

    /* positive overflow (INT64_MAX + 1) */
    pos = 0;
    assert(bencode_decode("i9223372036854775808e", 21, &pos) == NULL);

    /* digits but no 'e' terminator */
    pos = 0;
    assert(bencode_decode("i42x", 4, &pos) == NULL);

    /* 'i' with nothing after it (i >= len before reading a digit) */
    pos = 0;
    assert(bencode_decode("i", 1, &pos) == NULL);

    /* 'i' then a byte below '0' (data[i] < '0') */
    pos = 0;
    assert(bencode_decode("i/e", 3, &pos) == NULL);

    /* 'i' then a byte above '9' (data[i] > '9') */
    pos = 0;
    assert(bencode_decode("ize", 3, &pos) == NULL);

    /* '-' with no digit and end-of-buffer (i >= len after the sign) */
    pos = 0;
    assert(bencode_decode("i-", 2, &pos) == NULL);

    /* a digit followed by a byte below '0' mid-scan (loop's data[i] >= '0' false arm) */
    pos = 0;
    assert(bencode_decode("i5/e", 4, &pos) == NULL);

    printf("  test_decode_int_edges: OK\n");
}

static void test_decode_string_edges(void) {
    size_t pos;

    /* leading non-digit, non-marker byte: decode_depth falls through to NULL */
    pos = 0;
    assert(bencode_decode("abc:data", 8, &pos) == NULL);

    /* length digits but no ':' separator */
    pos = 0;
    assert(bencode_decode("5hello", 6, &pos) == NULL);

    /* declared length runs past the buffer */
    pos = 0;
    assert(bencode_decode("10:hello", 8, &pos) == NULL);

    /* length digits that consume the whole buffer (p >= len before ':') */
    pos = 0;
    assert(bencode_decode("5", 1, &pos) == NULL);

    /* unknown leading marker */
    pos = 0;
    assert(bencode_decode("xi42e", 5, &pos) == NULL);

    /* leading byte below '0': decode_depth's digit test takes its false arm */
    pos = 0;
    assert(bencode_decode("-9e", 3, &pos) == NULL);
    pos = 0;
    assert(bencode_decode("?", 1, &pos) == NULL);

    printf("  test_decode_string_edges: OK\n");
}

static void test_decode_list_edges(void) {
    size_t pos;
    bencode_value_t *val;

    /* 'l' then a malformed item: inner decode fails, list freed */
    pos = 0;
    assert(bencode_decode("li42", 4, &pos) == NULL);

    /* one good item but missing the final 'e' */
    pos = 0;
    assert(bencode_decode("li42e", 5, &pos) == NULL);

    /* well-formed single-element list */
    pos = 0;
    val = bencode_decode("li42ee", 6, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_LIST);
    assert(val->val.list_val.count == 1);
    assert(val->val.list_val.items[0]->type == BENCODE_INT);
    assert(val->val.list_val.items[0]->val.int_val == 42);
    assert(pos == 6);
    bencode_free(val);

    printf("  test_decode_list_edges: OK\n");
}

static void test_decode_dict_edges(void) {
    size_t pos;
    bencode_value_t *val;

    /* non-string key (integer) is rejected */
    pos = 0;
    assert(bencode_decode("di5e", 4, &pos) == NULL);

    /* key present but value missing */
    pos = 0;
    assert(bencode_decode("d3:foo", 6, &pos) == NULL);

    /* key present, value malformed */
    pos = 0;
    assert(bencode_decode("d3:foox", 7, &pos) == NULL);

    /* key + value but no closing 'e' */
    pos = 0;
    assert(bencode_decode("d3:fooi42e", 10, &pos) == NULL);

    /* well-formed single-pair dict */
    pos = 0;
    val = bencode_decode("d3:fooi42ee", 11, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_DICT);
    assert(val->val.dict_val.count == 1);
    bencode_value_t *foo = bencode_dict_get(val, "foo");
    assert(foo != NULL);
    assert(foo->type == BENCODE_INT);
    assert(foo->val.int_val == 42);
    assert(pos == 11);
    bencode_free(val);

    printf("  test_decode_dict_edges: OK\n");
}

/* Decode a list and a dict large enough to force the items/keys/values arrays to
 * grow past their initial capacity of 8 (realloc-success branch). */
static void test_decode_capacity_growth(void) {
    char buf[256];
    size_t len, pos;
    bencode_value_t *val;
    int i;

    /* list of 10 integers */
    len = 0;
    buf[len++] = 'l';
    for (i = 0; i < 10; i++) {
        int n = snprintf(buf + len, sizeof(buf) - len, "i%de", i);
        len += (size_t)n;
    }
    buf[len++] = 'e';
    pos = 0;
    val = bencode_decode(buf, len, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_LIST);
    assert(val->val.list_val.count == 10);
    assert(val->val.list_val.capacity >= 10);
    bencode_free(val);

    /* dict of 10 string->int pairs */
    len = 0;
    buf[len++] = 'd';
    for (i = 0; i < 10; i++) {
        int n = snprintf(buf + len, sizeof(buf) - len, "1:%ci%de", 'a' + i, i);
        len += (size_t)n;
    }
    buf[len++] = 'e';
    pos = 0;
    val = bencode_decode(buf, len, &pos);
    assert(val != NULL);
    assert(val->type == BENCODE_DICT);
    assert(val->val.dict_val.count == 10);
    assert(val->val.dict_val.capacity >= 10);
    bencode_free(val);

    printf("  test_decode_capacity_growth: OK\n");
}

/* Helper: build a string value of exactly `data_len` bytes. */
static bencode_value_t *make_filler_string(size_t data_len) {
    char *buf = malloc(data_len);
    assert(buf != NULL);
    memset(buf, 'x', data_len);
    bencode_value_t *s = bencode_string_new(buf, data_len);
    assert(s != NULL);
    free(buf);
    return s;
}

/* The encode buffer starts at cap=1024 and only grows when `needed > cap`. Each
 * encoder (int/string/list/dict open and close) has its own growth block, and
 * the growing element over-allocates (new_cap = needed*2), so the FIRST element
 * to cross 1024 is the only one that grows. These cases pad the buffer so the
 * crossing byte is, in turn, an int, a nested list 'l', a nested dict 'd', a
 * list-closing 'e' and a dict-closing 'e' -- hitting every growth path. */
static void test_encode_buffer_growth(void) {
    size_t enc_len;
    char *enc;

    /* (a0) encode_string growth: a single ~2000-byte string overflows the
     * initial 1024 cap directly inside encode_string. */
    {
        bencode_value_t *s = make_filler_string(2000);
        enc = bencode_encode(s, &enc_len);
        assert(enc != NULL);
        assert(enc_len > 2000);
        free(enc);
        bencode_free(s);
    }

    /* (a) encode_int growth: list = [ <1016-byte string> , 0 ].
     * 'l'(->1) + string(1+5+1016=1022, fits) -> new_pos 1022; int "i0e" needs
     * 1025 > 1024 -> encode_int grows. */
    {
        bencode_value_t *list = bencode_list_new();
        assert(bencode_list_add(list, make_filler_string(1016)) == 0);
        assert(bencode_list_add(list, bencode_int_new(0)) == 0);
        enc = bencode_encode(list, &enc_len);
        assert(enc != NULL);
        free(enc);
        bencode_free(list);
    }

    /* (b) encode_list close growth: list = [ <1018-byte string> ].
     * 'l'(->1) + string(1+5+1018=1024, fits exactly) -> new_pos 1024; closing
     * 'e' needs 1025 > 1024 -> encode_list close grows. */
    {
        bencode_value_t *list = bencode_list_new();
        assert(bencode_list_add(list, make_filler_string(1018)) == 0);
        enc = bencode_encode(list, &enc_len);
        assert(enc != NULL);
        free(enc);
        bencode_free(list);
    }

    /* (c) encode_list open growth (nested): list = [ <1018-byte string>, [] ].
     * outer 'l' + string -> new_pos 1024; the inner list's opening 'l' needs
     * 1025 > 1024 -> encode_list open grows. */
    {
        bencode_value_t *outer = bencode_list_new();
        assert(bencode_list_add(outer, make_filler_string(1018)) == 0);
        assert(bencode_list_add(outer, bencode_list_new()) == 0);
        enc = bencode_encode(outer, &enc_len);
        assert(enc != NULL);
        free(enc);
        bencode_free(outer);
    }

    /* (d) encode_dict open growth (nested): list = [ <1018-byte string>, {} ].
     * the inner dict's opening 'd' is the byte at offset 1024 -> grows. */
    {
        bencode_value_t *outer = bencode_list_new();
        assert(bencode_list_add(outer, make_filler_string(1018)) == 0);
        assert(bencode_list_add(outer, bencode_dict_new()) == 0);
        enc = bencode_encode(outer, &enc_len);
        assert(enc != NULL);
        free(enc);
        bencode_free(outer);
    }

    /* (e) encode_dict close growth: dict = { <1015-byte key> : 0 }.
     * 'd'(->1) + key(1+(4+1015)=1020, prefix "1015:") -> new_pos 1021; value
     * "i0e" -> 1024 (fits); closing 'e' needs 1025 > 1024 -> encode_dict close
     * grows. The out_len == NULL path is exercised here too. */
    {
        size_t klen = 1015;
        char *key = malloc(klen + 1);
        assert(key != NULL);
        memset(key, 'k', klen);
        key[klen] = '\0';
        bencode_value_t *dict = bencode_dict_new();
        assert(bencode_dict_add(dict, key, bencode_int_new(0)) == 0);
        enc = bencode_encode(dict, NULL);   /* out_len == NULL */
        assert(enc != NULL);
        free(enc);
        bencode_free(dict);
        free(key);
    }

    printf("  test_encode_buffer_growth: OK\n");
}

/* Grow the dict and list builder arrays past the initial capacity of 8 via the
 * public add API (realloc-success branch in bencode_dict_add/bencode_list_add). */
static void test_builder_capacity_growth(void) {
    bencode_value_t *list = bencode_list_new();
    assert(list != NULL);
    for (int i = 0; i < 10; i++) {
        assert(bencode_list_add(list, bencode_int_new(i)) == 0);
    }
    assert(list->val.list_val.count == 10);
    assert(list->val.list_val.capacity >= 10);
    bencode_free(list);

    bencode_value_t *dict = bencode_dict_new();
    assert(dict != NULL);
    for (int i = 0; i < 10; i++) {
        char key[8];
        snprintf(key, sizeof(key), "k%d", i);
        assert(bencode_dict_add(dict, key, bencode_int_new(i)) == 0);
    }
    assert(dict->val.dict_val.count == 10);
    assert(dict->val.dict_val.capacity >= 10);
    bencode_free(dict);

    printf("  test_builder_capacity_growth: OK\n");
}

/* Argument-validation branches of the public mutator/accessor helpers. */
static void test_arg_validation(void) {
    bencode_value_t *dict = bencode_dict_new();
    bencode_value_t *list = bencode_list_new();
    bencode_value_t *anint = bencode_int_new(1);
    assert(dict && list && anint);

    /* bencode_dict_get on a non-dict and with NULL key */
    assert(bencode_dict_get(anint, "x") == NULL);
    assert(bencode_dict_get(dict, NULL) == NULL);

    /* bencode_dict_add rejects NULL dict, wrong type, NULL key, NULL val */
    assert(bencode_dict_add(NULL, "k", anint) == -1);
    assert(bencode_dict_add(list, "k", anint) == -1);
    assert(bencode_dict_add(dict, NULL, anint) == -1);
    assert(bencode_dict_add(dict, "k", NULL) == -1);

    /* bencode_list_add rejects NULL list, wrong type, NULL val */
    assert(bencode_list_add(NULL, anint) == -1);
    assert(bencode_list_add(dict, anint) == -1);
    assert(bencode_list_add(list, NULL) == -1);

    bencode_free(dict);
    bencode_free(list);
    bencode_free(anint);

    printf("  test_arg_validation: OK\n");
}

int main(void) {
    printf("test_bencode:\n");
    
    test_encode_int();
    test_encode_int_negative();
    test_encode_string();
    test_encode_list();
    test_encode_dict();
    test_decode_int();
    test_decode_string();
    test_decode_list();
    test_decode_dict();
    test_decode_empty();
    test_decode_truncated();
    test_decode_malformed();
    test_decode_overflow();
    test_decode_int_min();
    test_decode_depth();
    test_dict_get();
    test_free_null();
    test_encode_null();
    test_roundtrip();
    test_decode_int_edges();
    test_decode_string_edges();
    test_decode_list_edges();
    test_decode_dict_edges();
    test_decode_capacity_growth();
    test_encode_buffer_growth();
    test_builder_capacity_growth();
    test_arg_validation();

    printf("test_bencode: OK\n");
    return 0;
}