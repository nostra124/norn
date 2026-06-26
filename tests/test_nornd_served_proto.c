/* Unit tests for the node-served KV stream protocol (FEAT-033). 100% cov. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "served_proto.h"

static void test_encode_req(void) {
    char out[512];
    assert(nornd_served_encode_req(NORND_SERVED_GET, "mykey", out, sizeof(out)) > 0);
    assert(strcmp(out, "GET mykey\n") == 0);
    assert(nornd_served_encode_req(NORND_SERVED_CAT, "abcd", out, sizeof(out)) > 0);
    assert(strcmp(out, "CAT abcd\n") == 0);
    assert(nornd_served_encode_req(NORND_SERVED_LIST, "pre", out, sizeof(out)) > 0);
    assert(strcmp(out, "LIST pre\n") == 0);
    /* LIST with empty/NULL arg is allowed */
    assert(nornd_served_encode_req(NORND_SERVED_LIST, NULL, out, sizeof(out)) > 0);
    assert(strcmp(out, "LIST\n") == 0);
    assert(nornd_served_encode_req(NORND_SERVED_LIST, "", out, sizeof(out)) > 0);

    /* GET/CAT need an arg */
    assert(nornd_served_encode_req(NORND_SERVED_GET, "", out, sizeof(out)) == -1);
    assert(nornd_served_encode_req(NORND_SERVED_CAT, NULL, out, sizeof(out)) == -1);
    /* arg too long */
    char big[NORND_SERVED_MAX_ARG + 2];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    assert(nornd_served_encode_req(NORND_SERVED_GET, big, out, sizeof(out)) == -1);
    /* won't fit */
    assert(nornd_served_encode_req(NORND_SERVED_GET, "mykey", out, 4) == -1);
    /* unknown verb value (defensive default) */
    assert(nornd_served_encode_req((nornd_served_verb_t)99, "x", out, sizeof(out)) == -1);
}

static void test_parse_req(void) {
    nornd_served_req_t r;
    /* null args */
    assert(nornd_served_parse_req(NULL, 0, &r) == -1);
    assert(nornd_served_parse_req("GET k\n", 6, NULL) == -1);

    /* GET with newline */
    assert(nornd_served_parse_req("GET mykey\n", 10, &r) == 0);
    assert(r.verb == NORND_SERVED_GET && r.arglen == 5 &&
           memcmp(r.arg, "mykey", 5) == 0);
    /* CAT without newline */
    assert(nornd_served_parse_req("CAT deadbeef", 12, &r) == 0);
    assert(r.verb == NORND_SERVED_CAT && r.arglen == 8);
    /* CRLF trimming */
    assert(nornd_served_parse_req("GET k\r\n", 7, &r) == 0);
    assert(r.verb == NORND_SERVED_GET && r.arglen == 1 && r.arg[0] == 'k');
    /* LIST with prefix */
    assert(nornd_served_parse_req("LIST blob/\n", 11, &r) == 0);
    assert(r.verb == NORND_SERVED_LIST && r.arglen == 5);
    /* LIST with no arg (allowed) */
    assert(nornd_served_parse_req("LIST\n", 5, &r) == 0);
    assert(r.verb == NORND_SERVED_LIST && r.arglen == 0);

    /* unknown verb of length 3 (GET/CAT slot) */
    assert(nornd_served_parse_req("FOO x\n", 6, &r) == -1);
    /* 4-char verb that isn't LIST (covers the LIST memcmp branch) */
    assert(nornd_served_parse_req("LISX y\n", 7, &r) == -1);
    /* unknown verb of another length */
    assert(nornd_served_parse_req("FROBNICATE x\n", 13, &r) == -1);
    /* zero-length input (covers the len>0 trim guards) */
    assert(nornd_served_parse_req("x", 0, &r) == -1);
    /* GET with no arg → required */
    assert(nornd_served_parse_req("GET\n", 4, &r) == -1);
    /* CAT with no arg */
    assert(nornd_served_parse_req("CAT", 3, &r) == -1);
    /* arg too long */
    char line[NORND_SERVED_MAX_ARG + 16];
    int n = snprintf(line, sizeof(line), "GET ");
    memset(line + n, 'y', NORND_SERVED_MAX_ARG + 1);
    assert(nornd_served_parse_req(line, n + NORND_SERVED_MAX_ARG + 1, &r) == -1);
}

static void test_encode_status(void) {
    char out[512];
    assert(nornd_served_encode_status(1, 1073741824ULL, NULL, out, sizeof(out)) > 0);
    assert(strcmp(out, "OK 1073741824\n") == 0);
    assert(nornd_served_encode_status(0, 0, "not found", out, sizeof(out)) > 0);
    assert(strcmp(out, "ERR not found\n") == 0);
    /* err NULL → "error" */
    assert(nornd_served_encode_status(0, 0, NULL, out, sizeof(out)) > 0);
    assert(strcmp(out, "ERR error\n") == 0);
    /* won't fit */
    assert(nornd_served_encode_status(1, 100, NULL, out, 3) == -1);
}

static void test_parse_status(void) {
    int ok;
    uint64_t len;
    char err[64];
    /* null args */
    assert(nornd_served_parse_status(NULL, 0, &ok, &len, err, sizeof(err)) == -1);
    assert(nornd_served_parse_status("OK 5\n", 5, NULL, &len, err, sizeof(err)) == -1);

    /* OK with length, newline */
    assert(nornd_served_parse_status("OK 4096\n", 8, &ok, &len, err, sizeof(err)) == 0);
    assert(ok == 1 && len == 4096);
    /* OK, len_out NULL */
    assert(nornd_served_parse_status("OK 7\n", 5, &ok, NULL, err, sizeof(err)) == 0);
    assert(ok == 1);
    /* OK with CRLF */
    assert(nornd_served_parse_status("OK 1\r\n", 6, &ok, &len, err, sizeof(err)) == 0);
    assert(ok == 1 && len == 1);
    /* "OK " with no number */
    assert(nornd_served_parse_status("OK \n", 4, &ok, &len, err, sizeof(err)) == -1);
    /* "OK " then non-digit above '9' */
    assert(nornd_served_parse_status("OK 12x\n", 7, &ok, &len, err, sizeof(err)) == -1);
    /* "OK " then a char below '0' (covers the other side of the digit test) */
    assert(nornd_served_parse_status("OK 1/2\n", 7, &ok, &len, err, sizeof(err)) == -1);

    /* ERR with message */
    assert(nornd_served_parse_status("ERR nope\n", 9, &ok, &len, err, sizeof(err)) == 0);
    assert(ok == 0 && strcmp(err, "nope") == 0);
    /* ERR, err buffer NULL (skip copy) */
    assert(nornd_served_parse_status("ERR boom\n", 9, &ok, &len, NULL, 0) == 0);
    assert(ok == 0);
    /* ERR, err non-NULL but errcap 0 (skip copy) */
    char zc[4];
    zc[0] = 'Z';
    assert(nornd_served_parse_status("ERR boom\n", 9, &ok, &len, zc, 0) == 0);
    assert(zc[0] == 'Z');
    /* zero-length input (covers the len>0 trim guards in parse_status) */
    assert(nornd_served_parse_status("x", 0, &ok, &len, err, sizeof(err)) == -1);
    /* ERR message truncated into a tiny buffer */
    char tiny[4];
    assert(nornd_served_parse_status("ERR abcdef\n", 11, &ok, &len, tiny, sizeof(tiny)) == 0);
    assert(strlen(tiny) == 3);

    /* neither OK nor ERR */
    assert(nornd_served_parse_status("WAT 1\n", 6, &ok, &len, err, sizeof(err)) == -1);
    /* too short to match either prefix */
    assert(nornd_served_parse_status("OK", 2, &ok, &len, err, sizeof(err)) == -1);
}

int main(void) {
    test_encode_req();
    test_parse_req();
    test_encode_status();
    test_parse_status();
    printf("all nornd served-proto tests passed\n");
    return 0;
}
