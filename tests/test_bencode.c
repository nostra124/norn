#include "bencode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    test_decode_depth();
    test_dict_get();
    test_free_null();
    test_encode_null();
    test_roundtrip();
    
    printf("test_bencode: OK\n");
    return 0;
}