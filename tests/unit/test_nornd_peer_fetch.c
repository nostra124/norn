/* SPDX-License-Identifier: MIT */
/* Unit tests for nornd_peer_fetch() validation (no network I/O). 100% cov. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sodium.h>
#include "peer_fetch.h"
#include "norn.h"

static const char HEX64[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static norn_client_t *make_client(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    norn_client_t *c = norn_new(pk, sk, NULL);
    assert(c);
    return c;
}

static void test_null_args(void) {
    unsigned char buf[64];
    size_t blen = sizeof(buf);
    char err[64];

    assert(nornd_peer_fetch(NULL, HEX64, "get", "k", buf, &blen, err, sizeof(err)) == -1);
    assert(nornd_peer_fetch((norn_client_t *)1, NULL, "get", "k", buf, &blen,
                            err, sizeof(err)) == -1);
    assert(nornd_peer_fetch((norn_client_t *)1, HEX64, NULL, "k", buf, &blen,
                            err, sizeof(err)) == -1);
    assert(nornd_peer_fetch((norn_client_t *)1, HEX64, "get", "k", NULL, &blen,
                            err, sizeof(err)) == -1);
    assert(nornd_peer_fetch((norn_client_t *)1, HEX64, "get", "k", buf, NULL,
                            err, sizeof(err)) == -1);
    /* NULL client, NULL err — no crash */
    assert(nornd_peer_fetch(NULL, HEX64, "get", "k", buf, &blen, NULL, 0) == -1);
}

static void test_spec_too_long(void) {
    norn_client_t *cli = make_client();
    char spec[200];
    memset(spec, 'a', 161);
    spec[161] = '\0';
    unsigned char buf[64];
    size_t blen = sizeof(buf);
    char err[64];

    assert(nornd_peer_fetch(cli, spec, "get", "k", buf, &blen, err, sizeof(err)) == -1);
    assert(strstr(err, "peer spec too long") != NULL);
    norn_free(cli);
}

static void test_spec_no_port_after_at(void) {
    norn_client_t *cli = make_client();
    char spec[200];
    snprintf(spec, sizeof(spec), "%s@hostonly", HEX64);
    unsigned char buf[64];
    size_t blen = sizeof(buf);
    char err[64];

    assert(nornd_peer_fetch(cli, spec, "get", "k", buf, &blen, err, sizeof(err)) == -1);
    assert(strstr(err, "endpoint must be host:port") != NULL);
    norn_free(cli);
}

static void test_bad_40_hex(void) {
    norn_client_t *cli = make_client();
    unsigned char buf[64];
    size_t blen = sizeof(buf);
    char err[64];

    assert(nornd_peer_fetch(cli, "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
                            "get", "k", buf, &blen, err, sizeof(err)) == -1);
    assert(strstr(err, "bad 40-hex node-id") != NULL);
    norn_free(cli);
}

static void test_bad_64_hex(void) {
    norn_client_t *cli = make_client();
    char spec[128];
    memset(spec, 'z', 64);
    spec[64] = '\0';
    unsigned char buf[64];
    size_t blen = sizeof(buf);
    char err[64];

    assert(nornd_peer_fetch(cli, spec, "get", "k", buf, &blen, err, sizeof(err)) == -1);
    assert(strstr(err, "bad 64-hex pubkey") != NULL);
    norn_free(cli);
}

static void test_spec_wrong_length(void) {
    norn_client_t *cli = make_client();
    unsigned char buf[64];
    size_t blen = sizeof(buf);
    char err[64];

    assert(nornd_peer_fetch(cli, "deadbeefdeadbeefdeadbeefdeadbeef",
                            "get", "k", buf, &blen, err, sizeof(err)) == -1);
    assert(strstr(err, "spec must be 40 or 64 hex chars") != NULL);
    norn_free(cli);
}

static void test_unknown_verb(void) {
    norn_client_t *cli = make_client();
    unsigned char buf[64];
    size_t blen = sizeof(buf);
    char err[64];

    assert(nornd_peer_fetch(cli, HEX64, "invalid", "k", buf, &blen, err, sizeof(err)) == -1);
    assert(strstr(err, "unknown verb") != NULL);
    norn_free(cli);
}

static void test_bad_host_inet(void) {
    norn_client_t *cli = make_client();
    char spec[200];
    snprintf(spec, sizeof(spec), "%s@notanip:1234", HEX64);
    unsigned char buf[64];
    size_t blen = sizeof(buf);
    char err[64];

    assert(nornd_peer_fetch(cli, spec, "get", "k", buf, &blen, err, sizeof(err)) == -1);
    assert(strstr(err, "bad host") != NULL);
    norn_free(cli);
}

static void test_req_encode_fail(void) {
    norn_client_t *cli = make_client();
    unsigned char buf[64];
    size_t blen = sizeof(buf);
    char err[64];

    /* GET with empty arg → encode_req fails before dial */
    assert(nornd_peer_fetch(cli, HEX64, "get", "", buf, &blen, err, sizeof(err)) == -1);
    assert(strstr(err, "bad served-KV request") != NULL);
    norn_free(cli);
}

static void test_null_err_ok(void) {
    /* Some validation paths with err=NULL/errcap=0 should still work. */
    norn_client_t *cli = make_client();
    unsigned char buf[64];
    size_t blen = sizeof(buf);

    assert(nornd_peer_fetch(cli, "deadbeefdeadbeefdeadbeefdeadbeef",
                            "get", "k", buf, &blen, NULL, 0) == -1);
    assert(nornd_peer_fetch(cli, HEX64, "invalid", "k", buf, &blen, NULL, 0) == -1);
    norn_free(cli);
}

int main(void) {
    if (sodium_init() < 0) return 1;
    test_null_args();
    test_spec_too_long();
    test_spec_no_port_after_at();
    test_bad_40_hex();
    test_bad_64_hex();
    test_spec_wrong_length();
    test_unknown_verb();
    test_bad_host_inet();
    test_req_encode_fail();
    test_null_err_ok();
    printf("all nornd peer-fetch tests passed\n");
    return 0;
}
