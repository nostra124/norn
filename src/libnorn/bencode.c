#include "bencode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* Cap recursion so a crafted packet of nested lists/dicts can't exhaust the C
 * stack (BEP messages are only a few levels deep). */
#define BENCODE_MAX_DEPTH 32
static bencode_value_t *decode_depth(const char *data, size_t len, size_t *pos, int depth);

/* Parse a base-10 integer from data[*pos..len) WITHOUT reading past `len`. KRPC
 * datagrams are bounded blobs, not C strings, so strtoll (which scans to a NUL
 * or non-digit) would over-read. On success advances *pos past the digits, fills
 * *out, returns 0; rejects empty input and overflow. `allow_neg` permits one
 * leading '-'. Also rejects non-canonical leading '+'/whitespace by construction. */
static int parse_int_bounded(const char *data, size_t len, size_t *pos,
                             int allow_neg, int64_t *out) {
    size_t i = *pos;
    int neg = 0;
    if (allow_neg && i < len && data[i] == '-') { neg = 1; i++; }
    if (i >= len || data[i] < '0' || data[i] > '9') return -1;   /* need >= 1 digit */
    uint64_t v = 0;
    for (; i < len && data[i] >= '0' && data[i] <= '9'; i++) {
        uint64_t nv = v * 10u + (uint64_t)(data[i] - '0');
        if (nv < v) return -1;                                   /* overflow */
        v = nv;
    }
    if (neg) {
        if (v > (uint64_t)INT64_MAX + 1) return -1;
        *out = (v == (uint64_t)INT64_MAX + 1) ? INT64_MIN : -(int64_t)v;
    } else {
        if (v > (uint64_t)INT64_MAX) return -1;
        *out = (int64_t)v;
    }
    *pos = i;
    return 0;
}

static bencode_value_t *decode_int(const char *data, size_t len, size_t *pos) {
    if (*pos >= len || data[*pos] != 'i') return NULL;
    size_t p = *pos + 1;
    int64_t val;
    if (parse_int_bounded(data, len, &p, 1, &val) != 0) return NULL;
    if (p >= len || data[p] != 'e') return NULL;
    *pos = p + 1;

    bencode_value_t *result = malloc(sizeof(bencode_value_t));
    if (!result) return NULL;
    result->type = BENCODE_INT;
    result->val.int_val = val;
    return result;
}

static bencode_value_t *decode_string(const char *data, size_t len, size_t *pos) {
    if (*pos >= len) return NULL;

    size_t p = *pos;
    int64_t slen;
    if (parse_int_bounded(data, len, &p, 0, &slen) != 0) return NULL;  /* no '-' for a length */
    if (p >= len || data[p] != ':') return NULL;

    size_t str_start = p + 1;                                          /* p < len, so str_start <= len */
    if ((size_t)slen > len - str_start) return NULL;
    
    bencode_value_t *result = malloc(sizeof(bencode_value_t));
    if (!result) return NULL;
    result->type = BENCODE_STRING;
    result->val.str_val.len = slen;
    result->val.str_val.data = malloc(slen + 1);
    if (!result->val.str_val.data) {
        free(result);
        return NULL;
    }
    memcpy(result->val.str_val.data, data + str_start, slen);
    result->val.str_val.data[slen] = '\0';
    
    *pos = str_start + slen;
    return result;
}

static bencode_value_t *decode_list(const char *data, size_t len, size_t *pos, int depth) {
    if (*pos >= len || data[*pos] != 'l') return NULL;
    (*pos)++;

    bencode_value_t *result = malloc(sizeof(bencode_value_t));
    if (!result) return NULL;
    result->type = BENCODE_LIST;
    result->val.list_val.items = NULL;
    result->val.list_val.count = 0;
    result->val.list_val.capacity = 0;

    while (*pos < len && data[*pos] != 'e') {
        bencode_value_t *item = decode_depth(data, len, pos, depth + 1);
        if (!item) {
            bencode_free(result);
            return NULL;
        }
        
        if (result->val.list_val.count >= result->val.list_val.capacity) {
            size_t new_cap = result->val.list_val.capacity == 0 ? 8 : result->val.list_val.capacity * 2;
            bencode_value_t **new_items = realloc(result->val.list_val.items, new_cap * sizeof(bencode_value_t *));
            if (!new_items) {
                bencode_free(item);
                bencode_free(result);
                return NULL;
            }
            result->val.list_val.items = new_items;
            result->val.list_val.capacity = new_cap;
        }
        result->val.list_val.items[result->val.list_val.count++] = item;
    }
    
    if (*pos >= len || data[*pos] != 'e') {
        bencode_free(result);
        return NULL;
    }
    (*pos)++;
    return result;
}

static bencode_value_t *decode_dict(const char *data, size_t len, size_t *pos, int depth) {
    if (*pos >= len || data[*pos] != 'd') return NULL;
    (*pos)++;
    
    bencode_value_t *result = malloc(sizeof(bencode_value_t));
    if (!result) return NULL;
    result->type = BENCODE_DICT;
    result->val.dict_val.keys = NULL;
    result->val.dict_val.values = NULL;
    result->val.dict_val.count = 0;
    result->val.dict_val.capacity = 0;
    
    while (*pos < len && data[*pos] != 'e') {
        bencode_value_t *key = decode_string(data, len, pos);
        if (!key) {
            bencode_free(result);
            return NULL;
        }
        
        bencode_value_t *value = decode_depth(data, len, pos, depth + 1);
        if (!value) {
            bencode_free(key);
            bencode_free(result);
            return NULL;
        }
        
        if (result->val.dict_val.count >= result->val.dict_val.capacity) {
            size_t new_cap = result->val.dict_val.capacity == 0 ? 8 : result->val.dict_val.capacity * 2;
            char **new_keys = realloc(result->val.dict_val.keys, new_cap * sizeof(char *));
            bencode_value_t **new_values = realloc(result->val.dict_val.values, new_cap * sizeof(bencode_value_t *));
            if (!new_keys || !new_values) {
                bencode_free(key);
                bencode_free(value);
                bencode_free(result);
                return NULL;
            }
            result->val.dict_val.keys = new_keys;
            result->val.dict_val.values = new_values;
            result->val.dict_val.capacity = new_cap;
        }
        
        result->val.dict_val.keys[result->val.dict_val.count] = key->val.str_val.data;
        result->val.dict_val.values[result->val.dict_val.count] = value;
        result->val.dict_val.count++;
        free(key);
    }
    
    if (*pos >= len || data[*pos] != 'e') {
        bencode_free(result);
        return NULL;
    }
    (*pos)++;
    return result;
}

static bencode_value_t *decode_depth(const char *data, size_t len, size_t *pos, int depth) {
    if (!data || !pos || *pos >= len) return NULL;
    if (depth > BENCODE_MAX_DEPTH) return NULL;   /* reject pathologically nested input */

    size_t p = *pos;
    bencode_value_t *result = NULL;
    char c = data[p];

    if (c == 'i') {
        result = decode_int(data, len, &p);
    } else if (c >= '0' && c <= '9') {
        result = decode_string(data, len, &p);
    } else if (c == 'l') {
        result = decode_list(data, len, &p, depth);
    } else if (c == 'd') {
        result = decode_dict(data, len, &p, depth);
    }

    if (result) *pos = p;
    return result;
}

bencode_value_t *bencode_decode(const char *data, size_t len, size_t *pos) {
    if (!data || len == 0 || !pos) return NULL;
    return decode_depth(data, len, pos, 0);
}

void bencode_free(bencode_value_t *val) {
    if (!val) return;
    
    switch (val->type) {
        case BENCODE_INT:
            break;
        case BENCODE_STRING:
            free(val->val.str_val.data);
            break;
        case BENCODE_LIST:
            for (size_t i = 0; i < val->val.list_val.count; i++) {
                bencode_free(val->val.list_val.items[i]);
            }
            free(val->val.list_val.items);
            break;
        case BENCODE_DICT:
            for (size_t i = 0; i < val->val.dict_val.count; i++) {
                free(val->val.dict_val.keys[i]);
                bencode_free(val->val.dict_val.values[i]);
            }
            free(val->val.dict_val.keys);
            free(val->val.dict_val.values);
            break;
    }
    free(val);
}

static size_t encode_int(char **buf, size_t *cap, size_t pos, int64_t val) {
    char tmp[32];
    int len = snprintf(tmp, sizeof(tmp), "i%llde", (long long)val);
    
    size_t needed = pos + len;
    if (needed > *cap) {
        size_t new_cap = needed * 2;
        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf) return 0;
        *buf = new_buf;
        *cap = new_cap;
    }
    
    memcpy(*buf + pos, tmp, len);
    return pos + len;
}

static size_t encode_string(char **buf, size_t *cap, size_t pos, const char *data, size_t len) {
    char tmp[32];
    int prefix_len = snprintf(tmp, sizeof(tmp), "%zu:", len);
    size_t needed = pos + prefix_len + len;
    
    if (needed > *cap) {
        size_t new_cap = needed * 2;
        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf) return 0;
        *buf = new_buf;
        *cap = new_cap;
    }
    
    memcpy(*buf + pos, tmp, prefix_len);
    memcpy(*buf + pos + prefix_len, data, len);
    return pos + prefix_len + len;
}

static size_t encode_value(char **buf, size_t *cap, size_t pos, const bencode_value_t *val);

static size_t encode_list(char **buf, size_t *cap, size_t pos, const bencode_value_t *val) {
    size_t new_pos = pos;
    
    size_t needed = pos + 1;
    if (needed > *cap) {
        size_t new_cap = needed * 2;
        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf) return 0;
        *buf = new_buf;
        *cap = new_cap;
    }
    (*buf)[new_pos++] = 'l';
    
    for (size_t i = 0; i < val->val.list_val.count; i++) {
        new_pos = encode_value(buf, cap, new_pos, val->val.list_val.items[i]);
        if (new_pos == 0) return 0;
    }
    
    needed = new_pos + 1;
    if (needed > *cap) {
        size_t new_cap = needed * 2;
        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf) return 0;
        *buf = new_buf;
        *cap = new_cap;
    }
    (*buf)[new_pos++] = 'e';
    
    return new_pos;
}

static size_t encode_dict(char **buf, size_t *cap, size_t pos, const bencode_value_t *val) {
    size_t new_pos = pos;
    
    size_t needed = pos + 1;
    if (needed > *cap) {
        size_t new_cap = needed * 2;
        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf) return 0;
        *buf = new_buf;
        *cap = new_cap;
    }
    (*buf)[new_pos++] = 'd';
    
    for (size_t i = 0; i < val->val.dict_val.count; i++) {
        new_pos = encode_string(buf, cap, new_pos, val->val.dict_val.keys[i], strlen(val->val.dict_val.keys[i]));
        if (new_pos == 0) return 0;
        new_pos = encode_value(buf, cap, new_pos, val->val.dict_val.values[i]);
        if (new_pos == 0) return 0;
    }
    
    needed = new_pos + 1;
    if (needed > *cap) {
        size_t new_cap = needed * 2;
        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf) return 0;
        *buf = new_buf;
        *cap = new_cap;
    }
    (*buf)[new_pos++] = 'e';
    
    return new_pos;
}

static size_t encode_value(char **buf, size_t *cap, size_t pos, const bencode_value_t *val) {
    switch (val->type) {
        case BENCODE_INT:
            return encode_int(buf, cap, pos, val->val.int_val);
        case BENCODE_STRING:
            return encode_string(buf, cap, pos, val->val.str_val.data, val->val.str_val.len);
        case BENCODE_LIST:
            return encode_list(buf, cap, pos, val);
        case BENCODE_DICT:
            return encode_dict(buf, cap, pos, val);
    }
    return 0;
}

char *bencode_encode(const bencode_value_t *val, size_t *out_len) {
    if (!val) return NULL;
    
    size_t cap = 1024;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    
    size_t len = encode_value(&buf, &cap, 0, val);
    if (len == 0) {
        free(buf);
        return NULL;
    }
    
    if (out_len) *out_len = len;
    return buf;
}

bencode_value_t *bencode_dict_get(const bencode_value_t *dict, const char *key) {
    if (!dict || dict->type != BENCODE_DICT || !key) return NULL;
    
    for (size_t i = 0; i < dict->val.dict_val.count; i++) {
        if (strcmp(dict->val.dict_val.keys[i], key) == 0) {
            return dict->val.dict_val.values[i];
        }
    }
    return NULL;
}

int bencode_dict_add(bencode_value_t *dict, const char *key, bencode_value_t *val) {
    if (!dict || dict->type != BENCODE_DICT || !key || !val) return -1;
    
    size_t key_len = strlen(key);
    
    if (dict->val.dict_val.count >= dict->val.dict_val.capacity) {
        size_t new_cap = dict->val.dict_val.capacity == 0 ? 8 : dict->val.dict_val.capacity * 2;
        char **new_keys = realloc(dict->val.dict_val.keys, new_cap * sizeof(char *));
        bencode_value_t **new_values = realloc(dict->val.dict_val.values, new_cap * sizeof(bencode_value_t *));
        if (!new_keys || !new_values) return -1;
        dict->val.dict_val.keys = new_keys;
        dict->val.dict_val.values = new_values;
        dict->val.dict_val.capacity = new_cap;
    }
    
    dict->val.dict_val.keys[dict->val.dict_val.count] = malloc(key_len + 1);
    if (!dict->val.dict_val.keys[dict->val.dict_val.count]) return -1;
    memcpy(dict->val.dict_val.keys[dict->val.dict_val.count], key, key_len + 1);
    dict->val.dict_val.values[dict->val.dict_val.count] = val;
    dict->val.dict_val.count++;
    
    return 0;
}

int bencode_list_add(bencode_value_t *list, bencode_value_t *val) {
    if (!list || list->type != BENCODE_LIST || !val) return -1;
    
    if (list->val.list_val.count >= list->val.list_val.capacity) {
        size_t new_cap = list->val.list_val.capacity == 0 ? 8 : list->val.list_val.capacity * 2;
        bencode_value_t **new_items = realloc(list->val.list_val.items, new_cap * sizeof(bencode_value_t *));
        if (!new_items) return -1;
        list->val.list_val.items = new_items;
        list->val.list_val.capacity = new_cap;
    }
    
    list->val.list_val.items[list->val.list_val.count] = val;
    list->val.list_val.count++;
    
    return 0;
}

bencode_value_t *bencode_int_new(int64_t val) {
    bencode_value_t *result = malloc(sizeof(bencode_value_t));
    if (!result) return NULL;
    result->type = BENCODE_INT;
    result->val.int_val = val;
    return result;
}

bencode_value_t *bencode_string_new(const char *data, size_t len) {
    bencode_value_t *result = malloc(sizeof(bencode_value_t));
    if (!result) return NULL;
    result->type = BENCODE_STRING;
    result->val.str_val.len = len;
    result->val.str_val.data = malloc(len + 1);
    if (!result->val.str_val.data) {
        free(result);
        return NULL;
    }
    memcpy(result->val.str_val.data, data, len);
    result->val.str_val.data[len] = '\0';
    return result;
}

bencode_value_t *bencode_dict_new(void) {
    bencode_value_t *result = malloc(sizeof(bencode_value_t));
    if (!result) return NULL;
    result->type = BENCODE_DICT;
    result->val.dict_val.keys = NULL;
    result->val.dict_val.values = NULL;
    result->val.dict_val.count = 0;
    result->val.dict_val.capacity = 0;
    return result;
}

bencode_value_t *bencode_list_new(void) {
    bencode_value_t *result = malloc(sizeof(bencode_value_t));
    if (!result) return NULL;
    result->type = BENCODE_LIST;
    result->val.list_val.items = NULL;
    result->val.list_val.count = 0;
    result->val.list_val.capacity = 0;
    return result;
}