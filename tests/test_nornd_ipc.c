/* Unit tests for the nornd IPC codec (FEAT-027). Pure module → 100% coverage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ipc.h"

/* Build a frame from an ASCII bencode body (no NULs). */
static size_t mkframe(const char *body, unsigned char *out) {
    size_t n = strlen(body);
    out[0] = (unsigned char)(n >> 24);
    out[1] = (unsigned char)(n >> 16);
    out[2] = (unsigned char)(n >> 8);
    out[3] = (unsigned char)n;
    memcpy(out + 4, body, n);
    return 4 + n;
}

static void test_req_roundtrip(void) {
    nornd_ipc_req_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.op, "cas");
    memcpy(r.key, "k\x00y", 3);
    r.klen = 3;
    memcpy(r.val, "value", 5);
    r.vlen = 5;
    r.has_val = 1;
    memcpy(r.expect, "old", 3);
    r.elen = 3;
    r.has_expect = 1;
    memset(r.id, 0xAB, sizeof(r.id));
    r.has_id = 1;
    r.seq = -42;
    r.has_seq = 1;

    unsigned char buf[4096];
    int n = nornd_ipc_encode_req(&r, buf, sizeof(buf));
    assert(n > 0);
    assert(nornd_ipc_frame_len(buf, (size_t)n) == n - 4);

    nornd_ipc_req_t d;
    size_t consumed = 0;
    assert(nornd_ipc_decode_req(buf, (size_t)n, &d, &consumed) == 0);
    assert(consumed == (size_t)n);
    assert(strcmp(d.op, "cas") == 0);
    assert(d.klen == 3 && memcmp(d.key, "k\x00y", 3) == 0);
    assert(d.has_val && d.vlen == 5 && memcmp(d.val, "value", 5) == 0);
    assert(d.has_expect && d.elen == 3 && memcmp(d.expect, "old", 3) == 0);
    assert(d.has_id && d.id[0] == 0xAB);
    assert(d.has_seq && d.seq == -42);
}

static void test_req_minimal(void) {
    nornd_ipc_req_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.op, "members");
    unsigned char buf[256];
    int n = nornd_ipc_encode_req(&r, buf, sizeof(buf));
    assert(n > 0);
    nornd_ipc_req_t d;
    assert(nornd_ipc_decode_req(buf, (size_t)n, &d, NULL) == 0);
    assert(strcmp(d.op, "members") == 0);
    assert(d.klen == 0 && !d.has_val && !d.has_expect && !d.has_id && !d.has_seq);
}

static void test_req_encode_errors(void) {
    unsigned char buf[4096];
    nornd_ipc_req_t r;
    memset(&r, 0, sizeof(r));
    assert(nornd_ipc_encode_req(NULL, buf, sizeof(buf)) == -1);
    assert(nornd_ipc_encode_req(&r, NULL, sizeof(buf)) == -1); /* empty op (and NULL out) */
    strcpy(r.op, "get");
    assert(nornd_ipc_encode_req(&r, NULL, sizeof(buf)) == -1); /* NULL out */
    r.klen = NORND_IPC_MAX_KEY + 1;
    assert(nornd_ipc_encode_req(&r, buf, sizeof(buf)) == -1); /* klen too big */
    r.klen = 0;
    r.has_val = 1;
    r.vlen = NORND_IPC_MAX_VAL + 1;
    assert(nornd_ipc_encode_req(&r, buf, sizeof(buf)) == -1); /* vlen too big */
    r.vlen = 1;
    r.has_expect = 1;
    r.elen = NORND_IPC_MAX_VAL + 1;
    assert(nornd_ipc_encode_req(&r, buf, sizeof(buf)) == -1); /* elen too big */
    /* cap too small */
    memset(&r, 0, sizeof(r));
    strcpy(r.op, "get");
    assert(nornd_ipc_encode_req(&r, buf, 3) == -1);
}

static void test_req_decode_malformed(void) {
    unsigned char buf[4096];
    nornd_ipc_req_t d;
    /* NULL args */
    assert(nornd_ipc_decode_req(NULL, 10, &d, NULL) == -1);
    assert(nornd_ipc_decode_req(buf, 10, NULL, NULL) == -1);
    /* < 4 bytes (no length prefix) */
    assert(nornd_ipc_decode_req(buf, 2, &d, NULL) == -1);
    /* length prefix bigger than MAX_BODY */
    buf[0] = 0; buf[1] = 0x02; buf[2] = 0; buf[3] = 0; /* 131072 > 65536 */
    assert(nornd_ipc_decode_req(buf, 8, &d, NULL) == -1);
    /* incomplete body */
    size_t n = mkframe("d2:op3:gete", buf);
    assert(nornd_ipc_decode_req(buf, n - 1, &d, NULL) == -1);
    /* not a dict */
    n = mkframe("i5e", buf);
    assert(nornd_ipc_decode_req(buf, n, &d, NULL) == -1);
    /* invalid bencode */
    n = mkframe("xxxx", buf);
    assert(nornd_ipc_decode_req(buf, n, &d, NULL) == -1);
    /* missing op */
    n = mkframe("de", buf);
    assert(nornd_ipc_decode_req(buf, n, &d, NULL) == -1);
    /* op wrong type (int) */
    n = mkframe("d2:opi5ee", buf);
    assert(nornd_ipc_decode_req(buf, n, &d, NULL) == -1);
    /* op empty */
    n = mkframe("d2:op0:e", buf);
    assert(nornd_ipc_decode_req(buf, n, &d, NULL) == -1);
    /* op too long (>= MAX_OP=24) */
    n = mkframe("d2:op30:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaae", buf);
    assert(nornd_ipc_decode_req(buf, n, &d, NULL) == -1);
    /* key wrong type */
    n = mkframe("d2:op3:get3:keyi1ee", buf);
    assert(nornd_ipc_decode_req(buf, n, &d, NULL) == -1);
    /* id wrong length (not 32) */
    n = mkframe("d2:op3:get2:id3:abce", buf);
    assert(nornd_ipc_decode_req(buf, n, &d, NULL) == -1);
    /* id wrong type */
    n = mkframe("d2:op3:get2:idi1ee", buf);
    assert(nornd_ipc_decode_req(buf, n, &d, NULL) == -1);
    /* seq wrong type */
    n = mkframe("d2:op3:get3:seq3:abce", buf);
    assert(nornd_ipc_decode_req(buf, n, &d, NULL) == -1);
}

static void test_req_id_and_oversize(void) {
    unsigned char buf[4096];
    nornd_ipc_req_t d;
    /* valid 32-byte id */
    char body[128];
    memcpy(body, "d2:op3:get2:id32:", 17);
    for (int i = 0; i < 32; i++) body[17 + i] = (char)('A' + (i % 26));
    body[17 + 32] = 'e';
    size_t blen = 17 + 32 + 1;
    buf[0] = 0; buf[1] = 0; buf[2] = (unsigned char)(blen >> 8); buf[3] = (unsigned char)blen;
    memcpy(buf + 4, body, blen);
    assert(nornd_ipc_decode_req(buf, 4 + blen, &d, NULL) == 0);
    assert(d.has_id && d.id[0] == 'A' && d.id[31] == (unsigned char)('A' + (31 % 26)));

    /* oversized val field (> MAX_VAL) is rejected on decode */
    static char big[NORND_IPC_MAX_VAL + 64];
    int hl = snprintf(big, sizeof(big), "d2:op3:get3:val%d:", NORND_IPC_MAX_VAL + 1);
    memset(big + hl, 'x', NORND_IPC_MAX_VAL + 1);
    big[hl + NORND_IPC_MAX_VAL + 1] = 'e';
    size_t tl = (size_t)hl + NORND_IPC_MAX_VAL + 1 + 1;
    static unsigned char fb[NORND_IPC_MAX_VAL + 128];
    fb[0] = (unsigned char)(tl >> 24); fb[1] = (unsigned char)(tl >> 16);
    fb[2] = (unsigned char)(tl >> 8); fb[3] = (unsigned char)tl;
    memcpy(fb + 4, big, tl);
    assert(nornd_ipc_decode_req(fb, 4 + tl, &d, NULL) == -1);
}

static void test_resp_roundtrip(void) {
    nornd_ipc_resp_t r;
    memset(&r, 0, sizeof(r));
    r.ok = 1;
    memcpy(r.val, "hello", 5);
    r.vlen = 5;
    r.has_val = 1;
    r.n_items = 2;
    memcpy(r.items[0].data, "alice", 5);
    r.items[0].len = 5;
    memcpy(r.items[1].data, "bob", 3);
    r.items[1].len = 3;

    unsigned char buf[4096];
    int n = nornd_ipc_encode_resp(&r, buf, sizeof(buf));
    assert(n > 0);
    nornd_ipc_resp_t d;
    size_t consumed = 0;
    assert(nornd_ipc_decode_resp(buf, (size_t)n, &d, &consumed) == 0);
    assert(consumed == (size_t)n);
    assert(d.ok == 1 && d.has_val && d.vlen == 5 && memcmp(d.val, "hello", 5) == 0);
    assert(d.n_items == 2 && d.items[0].len == 5 && memcmp(d.items[0].data, "alice", 5) == 0);
    assert(d.items[1].len == 3 && memcmp(d.items[1].data, "bob", 3) == 0);

    /* error response */
    memset(&r, 0, sizeof(r));
    r.ok = 0;
    strcpy(r.err, "not found");
    r.has_err = 1;
    n = nornd_ipc_encode_resp(&r, buf, sizeof(buf));
    assert(n > 0);
    assert(nornd_ipc_decode_resp(buf, (size_t)n, &d, NULL) == 0);
    assert(d.ok == 0 && d.has_err && strcmp(d.err, "not found") == 0 && d.n_items == 0);
}

static void test_resp_encode_errors(void) {
    unsigned char buf[4096];
    nornd_ipc_resp_t r;
    memset(&r, 0, sizeof(r));
    assert(nornd_ipc_encode_resp(NULL, buf, sizeof(buf)) == -1);
    assert(nornd_ipc_encode_resp(&r, NULL, sizeof(buf)) == -1);
    r.vlen = NORND_IPC_MAX_VAL + 1;
    r.has_val = 1;
    assert(nornd_ipc_encode_resp(&r, buf, sizeof(buf)) == -1);
    r.vlen = 0; r.has_val = 0;
    r.n_items = NORND_IPC_MAX_ITEMS + 1;
    assert(nornd_ipc_encode_resp(&r, buf, sizeof(buf)) == -1);
    r.n_items = -1;
    assert(nornd_ipc_encode_resp(&r, buf, sizeof(buf)) == -1);
    /* cap too small */
    memset(&r, 0, sizeof(r));
    r.ok = 1;
    assert(nornd_ipc_encode_resp(&r, buf, 3) == -1);
}

static void test_resp_decode_malformed(void) {
    unsigned char buf[4096];
    nornd_ipc_resp_t d;
    assert(nornd_ipc_decode_resp(NULL, 10, &d, NULL) == -1);
    assert(nornd_ipc_decode_resp(buf, 10, NULL, NULL) == -1);
    size_t n = mkframe("i5e", buf); /* not a dict */
    assert(nornd_ipc_decode_resp(buf, n, &d, NULL) == -1);
    n = mkframe("de", buf); /* missing ok */
    assert(nornd_ipc_decode_resp(buf, n, &d, NULL) == -1);
    n = mkframe("d2:ok3:abce", buf); /* ok wrong type */
    assert(nornd_ipc_decode_resp(buf, n, &d, NULL) == -1);
    n = mkframe("d2:oki1e5:itemsi1ee", buf); /* items wrong type */
    assert(nornd_ipc_decode_resp(buf, n, &d, NULL) == -1);
    /* an item of wrong type inside the list */
    n = mkframe("d2:oki1e5:itemsli1eee", buf);
    assert(nornd_ipc_decode_resp(buf, n, &d, NULL) == -1);
    /* err wrong type */
    n = mkframe("d2:oki1e3:erri5ee", buf);
    assert(nornd_ipc_decode_resp(buf, n, &d, NULL) == -1);
    /* ok=0 path */
    n = mkframe("d2:oki0ee", buf);
    assert(nornd_ipc_decode_resp(buf, n, &d, NULL) == 0 && d.ok == 0);
}

/* Frame a binary body of length blen. */
static size_t mkframe_n(const unsigned char *body, size_t blen, unsigned char *out) {
    out[0] = (unsigned char)(blen >> 24);
    out[1] = (unsigned char)(blen >> 16);
    out[2] = (unsigned char)(blen >> 8);
    out[3] = (unsigned char)blen;
    memcpy(out + 4, body, blen);
    return 4 + blen;
}

static void test_more_coverage(void) {
    static unsigned char body[NORND_IPC_MAX_VAL + 256];
    static unsigned char fb[NORND_IPC_MAX_VAL + 384];
    unsigned char small[256];

    /* encode_req with an empty op but a valid out buffer → op[0]=='\0'. */
    nornd_ipc_req_t er;
    memset(&er, 0, sizeof(er));
    assert(nornd_ipc_encode_req(&er, small, sizeof(small)) == -1);

    /* decode req: oversized 'expect' field. */
    int hl = snprintf((char *)body, sizeof(body), "d2:op3:get6:expect%d:", NORND_IPC_MAX_VAL + 1);
    memset(body + hl, 'x', NORND_IPC_MAX_VAL + 1);
    body[hl + NORND_IPC_MAX_VAL + 1] = 'e';
    size_t bl = (size_t)hl + NORND_IPC_MAX_VAL + 1 + 1;
    size_t fl = mkframe_n(body, bl, fb);
    nornd_ipc_req_t dr;
    assert(nornd_ipc_decode_req(fb, fl, &dr, NULL) == -1);

    /* decode resp: oversized 'val' field. */
    hl = snprintf((char *)body, sizeof(body), "d2:oki1e3:val%d:", NORND_IPC_MAX_VAL + 1);
    memset(body + hl, 'x', NORND_IPC_MAX_VAL + 1);
    body[hl + NORND_IPC_MAX_VAL + 1] = 'e';
    bl = (size_t)hl + NORND_IPC_MAX_VAL + 1 + 1;
    fl = mkframe_n(body, bl, fb);
    nornd_ipc_resp_t dresp;
    assert(nornd_ipc_decode_resp(fb, fl, &dresp, NULL) == -1);

    /* decode resp: an item longer than NORND_IPC_MAX_ITEM. */
    hl = snprintf((char *)body, sizeof(body), "d2:oki1e5:itemsl%d:", NORND_IPC_MAX_ITEM + 1);
    memset(body + hl, 'x', NORND_IPC_MAX_ITEM + 1);
    body[hl + NORND_IPC_MAX_ITEM + 1] = 'e';     /* end list */
    body[hl + NORND_IPC_MAX_ITEM + 2] = 'e';     /* end dict */
    bl = (size_t)hl + NORND_IPC_MAX_ITEM + 1 + 2;
    fl = mkframe_n(body, bl, fb);
    assert(nornd_ipc_decode_resp(fb, fl, &dresp, NULL) == -1);

    /* decode resp: first item bad, more follow → loop exits via `ok`. */
    size_t fn = mkframe("d2:oki1e5:itemsli1e1:xee", fb);
    assert(nornd_ipc_decode_resp(fb, fn, &dresp, NULL) == -1);

    /* decode resp: too many items (> NORND_IPC_MAX_ITEMS). */
    size_t o = (size_t)snprintf((char *)body, sizeof(body), "d2:oki1e5:itemsl");
    for (int i = 0; i < NORND_IPC_MAX_ITEMS + 1; i++) {
        memcpy(body + o, "1:x", 3);
        o += 3;
    }
    body[o++] = 'e';
    body[o++] = 'e';
    fl = mkframe_n(body, o, fb);
    assert(nornd_ipc_decode_resp(fb, fl, &dresp, NULL) == -1);
}

static void test_frame_len(void) {
    unsigned char buf[8] = {0x00, 0x00, 0x01, 0x00, 'x', 'x', 'x', 'x'};
    assert(nornd_ipc_frame_len(buf, 8) == 256);
    assert(nornd_ipc_frame_len(buf, 3) == -1);
    assert(nornd_ipc_frame_len(NULL, 8) == -1);
}

int main(void) {
    test_req_roundtrip();
    test_req_minimal();
    test_req_encode_errors();
    test_req_decode_malformed();
    test_req_id_and_oversize();
    test_resp_roundtrip();
    test_resp_encode_errors();
    test_resp_decode_malformed();
    test_more_coverage();
    test_frame_len();
    printf("test_nornd_ipc: all passed\n");
    return 0;
}
