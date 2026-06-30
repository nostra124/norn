/* SPDX-License-Identifier: MIT */
#ifndef BENCODE_H
#define BENCODE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BENCODE_INT,
    BENCODE_STRING,
    BENCODE_LIST,
    BENCODE_DICT
} bencode_type_t;

typedef struct bencode_value {
    bencode_type_t type;
    union {
        int64_t int_val;
        struct {
            char *data;
            size_t len;
        } str_val;
        struct {
            struct bencode_value **items;
            size_t count;
            size_t capacity;
        } list_val;
        struct {
            char **keys;
            struct bencode_value **values;
            size_t count;
            size_t capacity;
        } dict_val;
    } val;
} bencode_value_t;

bencode_value_t *bencode_decode(const char *data, size_t len, size_t *pos);
void bencode_free(bencode_value_t *val);
char *bencode_encode(const bencode_value_t *val, size_t *out_len);

bencode_value_t *bencode_dict_get(const bencode_value_t *dict, const char *key);
int bencode_dict_add(bencode_value_t *dict, const char *key, bencode_value_t *val);
int bencode_list_add(bencode_value_t *list, bencode_value_t *val);

bencode_value_t *bencode_int_new(int64_t val);
bencode_value_t *bencode_string_new(const char *data, size_t len);
bencode_value_t *bencode_dict_new(void);
bencode_value_t *bencode_list_new(void);

#endif