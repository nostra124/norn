/* SPDX-License-Identifier: MIT */
/* Test transport vtable dispatchers and NULL-safety. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "transport.h"

static norn_mode_t mock_modes(const norn_transport_t *t) { (void)t; return NORN_DATAGRAM; }
static uint32_t mock_cap(const norn_transport_t *t) { (void)t; return 0x42; }
static int mock_dial(norn_transport_t *t, const void *ep, size_t eplen) { (void)t; (void)ep; (void)eplen; return 0; }
static int mock_send(norn_transport_t *t, const void *buf, size_t len) { (void)t; (void)buf; return (int)len; }
static int mock_recv(norn_transport_t *t, void *buf, size_t cap) { (void)t; (void)buf; (void)cap; return 10; }
static int mock_local_endpoint(norn_transport_t *t, void *out, size_t cap) { (void)t; (void)out; (void)cap; return 5; }
static void mock_close(norn_transport_t *t) { (void)t; }

static const norn_transport_ops_t MOCK_OPS = {
    mock_modes, mock_cap, mock_dial, mock_send, mock_recv, mock_local_endpoint, mock_close,
};

/* Mode helpers */
static norn_mode_t stream_modes(const norn_transport_t *t) { (void)t; return NORN_STREAM; }
static norn_mode_t both_modes(const norn_transport_t *t) { (void)t; return NORN_BOTH; }

static const norn_transport_ops_t STREAM_ONLY_OPS = {
    stream_modes, mock_cap, mock_dial, mock_send, mock_recv, mock_local_endpoint, mock_close,
};

static const norn_transport_ops_t BOTH_MODES_OPS = {
    both_modes, mock_cap, mock_dial, mock_send, mock_recv, mock_local_endpoint, mock_close,
};

/* NULL transport tests */
static void test_null_transport(void) {
    assert(norn_transport_modes(NULL) == (norn_mode_t)0);
    assert(norn_transport_cap(NULL) == 0);
    assert(norn_transport_is_datagram(NULL) == 0);
    assert(norn_transport_is_stream(NULL) == 0);
    assert(norn_transport_dial(NULL, NULL, 0) == -1);
    assert(norn_transport_send(NULL, NULL, 0) == -1);
    assert(norn_transport_recv(NULL, NULL, 0) == -1);
    assert(norn_transport_local_endpoint(NULL, NULL, 0) == -1);
    norn_transport_close(NULL);  /* should not crash */
}

/* NULL ops tests */
static void test_null_ops(void) {
    norn_transport_t t = { NULL, NULL };
    assert(norn_transport_modes(&t) == (norn_mode_t)0);
    assert(norn_transport_cap(&t) == 0);
    assert(norn_transport_is_datagram(&t) == 0);
    assert(norn_transport_is_stream(&t) == 0);
    assert(norn_transport_dial(&t, NULL, 0) == -1);
    assert(norn_transport_send(&t, NULL, 0) == -1);
    assert(norn_transport_recv(&t, NULL, 0) == -1);
    assert(norn_transport_local_endpoint(&t, NULL, 0) == -1);
    norn_transport_close(&t);  /* should not crash */
}

/* Partial ops (NULL function pointers) */
static void test_partial_ops(void) {
    static const norn_transport_ops_t partial = {
        NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    };
    norn_transport_t t = { &partial, NULL };
    assert(norn_transport_modes(&t) == (norn_mode_t)0);
    assert(norn_transport_cap(&t) == 0);
    assert(norn_transport_dial(&t, NULL, 0) == -1);
    assert(norn_transport_send(&t, NULL, 0) == -1);
    assert(norn_transport_recv(&t, NULL, 0) == -1);
    assert(norn_transport_local_endpoint(&t, NULL, 0) == -1);
    norn_transport_close(&t);  /* should not crash */
}

/* Valid transport dispatch */
static void test_valid_transport(void) {
    norn_transport_t t = { &MOCK_OPS, NULL };
    assert(norn_transport_modes(&t) == NORN_DATAGRAM);
    assert(norn_transport_cap(&t) == 0x42);
    assert(norn_transport_is_datagram(&t) == 1);
    assert(norn_transport_is_stream(&t) == 0);
    assert(norn_transport_dial(&t, "endpoint", 8) == 0);
    assert(norn_transport_send(&t, "data", 4) == 4);
    assert(norn_transport_recv(&t, NULL, 100) == 10);
    assert(norn_transport_local_endpoint(&t, NULL, 100) == 5);
    norn_transport_close(&t);
}

/* Mode detection */
static void test_mode_detection(void) {
    norn_transport_t dg = { &MOCK_OPS, NULL };
    assert(norn_transport_is_datagram(&dg) == 1);
    assert(norn_transport_is_stream(&dg) == 0);
    
    norn_transport_t st = { &STREAM_ONLY_OPS, NULL };
    assert(norn_transport_is_datagram(&st) == 0);
    assert(norn_transport_is_stream(&st) == 1);
    
    norn_transport_t bt = { &BOTH_MODES_OPS, NULL };
    assert(norn_transport_is_datagram(&bt) == 1);
    assert(norn_transport_is_stream(&bt) == 1);
}

int main(void) {
    test_null_transport();
    test_null_ops();
    test_partial_ops();
    test_valid_transport();
    test_mode_detection();
    printf("transport tests passed\n");
    return 0;
}