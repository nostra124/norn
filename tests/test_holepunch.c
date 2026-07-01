/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sodium.h>

#include "norn.h"
#include "norn_nat.h"
#include "norn_rendezvous.h"
#include "norn_session.h"
#include "crypto.h"

static void test_holepunch_req_roundtrip(void) {
    norn_holepunch_req_t req, out;
    memset(&req, 0, sizeof(req));
    req.msg_type = NORN_MSG_HOLEPUNCH_REQ;
    memset(req.target_pubkey, 0xAA, 32);
    memset(req.my_ephemeral_pubkey, 0xBB, 32);
    req.my_external_ip = 0xC0A80101u;  /* 192.168.1.1 */
    req.my_external_port = 4321;
    memset(req.signature, 0xCC, 64);

    uint8_t buf[NORN_HOLEPUNCH_REQ_LEN];
    assert(norn_encode_holepunch_req(&req, buf) == 0);
    assert(norn_decode_holepunch_req(&out, buf, sizeof(buf)) == 0);

    assert(out.msg_type == req.msg_type);
    assert(memcmp(out.target_pubkey, req.target_pubkey, 32) == 0);
    assert(memcmp(out.my_ephemeral_pubkey, req.my_ephemeral_pubkey, 32) == 0);
    assert(out.my_external_ip == req.my_external_ip);
    assert(out.my_external_port == req.my_external_port);
    assert(memcmp(out.signature, req.signature, 64) == 0);

    printf("  holepunch_req roundtrip ok\n");
}

static void test_holepunch_resp_roundtrip(void) {
    norn_holepunch_resp_t resp, out;
    memset(&resp, 0, sizeof(resp));
    resp.msg_type = NORN_MSG_HOLEPUNCH_RESP;
    memset(resp.peer_pubkey, 0x11, 32);
    resp.peer_external_ip = 0x01020304u;
    resp.peer_external_port = 9999;
    memset(resp.peer_ephemeral_pubkey, 0x22, 32);
    memset(resp.signature, 0x33, 64);

    uint8_t buf[NORN_HOLEPUNCH_RESP_LEN];
    assert(norn_encode_holepunch_resp(&resp, buf) == 0);
    assert(norn_decode_holepunch_resp(&out, buf, sizeof(buf)) == 0);

    assert(out.msg_type == resp.msg_type);
    assert(memcmp(out.peer_pubkey, resp.peer_pubkey, 32) == 0);
    assert(out.peer_external_ip == resp.peer_external_ip);
    assert(out.peer_external_port == resp.peer_external_port);
    assert(memcmp(out.peer_ephemeral_pubkey, resp.peer_ephemeral_pubkey, 32) == 0);
    assert(memcmp(out.signature, resp.signature, 64) == 0);

    printf("  holepunch_resp roundtrip ok\n");
}

static void test_probe_roundtrip(void) {
    norn_probe_t probe, out;
    memset(&probe, 0, sizeof(probe));
    probe.msg_type = NORN_MSG_PROBE;
    memset(probe.ephemeral_pubkey, 0x77, 32);

    uint8_t buf[NORN_PROBE_LEN];
    assert(norn_encode_probe(&probe, buf) == 0);
    assert(norn_decode_probe(&out, buf, sizeof(buf)) == 0);

    assert(out.msg_type == probe.msg_type);
    assert(memcmp(out.ephemeral_pubkey, probe.ephemeral_pubkey, 32) == 0);

    printf("  probe roundtrip ok\n");
}

static void test_null_handling(void) {
    /* norn_hole_punch_async */
    assert(norn_hole_punch_async(NULL, NULL, NULL, NULL, NULL) == -1);

    keypair_t k;
    assert(crypto_keypair_new(&k) == 0);
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test/hp";
    norn_client_t *c = norn_new(k.public_key, k.secret_key, &cfg);
    assert(c != NULL);

    unsigned char dummy[32];
    memset(dummy, 1, 32);
    assert(norn_hole_punch_async(c, NULL, dummy, NULL, NULL) == -1);
    assert(norn_hole_punch_async(c, dummy, NULL, NULL, NULL) == -1);
    assert(norn_hole_punch_async(NULL, dummy, dummy, NULL, NULL) == -1);

    /* norn_rendezvous_enable */
    assert(norn_rendezvous_enable(NULL, NULL, NULL) == -1);

    norn_free(c);
    printf("  null handling ok\n");
}

static void test_rendezvous_enable(void) {
    keypair_t k;
    assert(crypto_keypair_new(&k) == 0);
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test/rv";
    norn_client_t *c = norn_new(k.public_key, k.secret_key, &cfg);
    assert(c != NULL);

    assert(norn_rendezvous_enable(c, NULL, NULL) == 0);

    norn_free(c);
    printf("  rendezvous_enable ok\n");
}

static void test_rendezvous_coordination(void) {
    /* norn_rendezvous_handle_req uses client->self_pub as the sender's
     * identity and client->self_sec for signing the response.  Each peer
     * calls it with their own client; the shared norn_rendezvous_t state
     * accumulates both halves until it can produce a response. */
    keypair_t init_kp, resp_kp;
    assert(crypto_keypair_new(&init_kp) == 0);
    assert(crypto_keypair_new(&resp_kp) == 0);

    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test/rv-coord";
    norn_client_t *init_c = norn_new(init_kp.public_key, init_kp.secret_key, &cfg);
    norn_client_t *resp_c = norn_new(resp_kp.public_key, resp_kp.secret_key, &cfg);
    assert(init_c && resp_c);

    norn_rendezvous_t state;
    assert(norn_rendezvous_init(&state) == 0);

    /* Initiator: "I want to reach resp_kp, here is my ephemeral + external addr." */
    norn_holepunch_req_t req_init;
    memset(&req_init, 0, sizeof(req_init));
    req_init.msg_type = NORN_MSG_HOLEPUNCH_REQ;
    memcpy(req_init.target_pubkey, resp_kp.public_key, 32);
    memset(req_init.my_ephemeral_pubkey, 0xAA, 32);
    req_init.my_external_ip = 0x0A000001u;
    req_init.my_external_port = 5001;

    norn_holepunch_resp_t resp_out;
    memset(&resp_out, 0, sizeof(resp_out));

    int rc = norn_rendezvous_handle_req(&state, &req_init,
                                        req_init.my_external_ip,
                                        req_init.my_external_port,
                                        init_c, &resp_out);
    assert(rc == 0);

    /* Responder: "I want to reach init_kp, here is my ephemeral + external addr." */
    norn_holepunch_req_t req_resp;
    memset(&req_resp, 0, sizeof(req_resp));
    req_resp.msg_type = NORN_MSG_HOLEPUNCH_REQ;
    memcpy(req_resp.target_pubkey, init_kp.public_key, 32);
    memset(req_resp.my_ephemeral_pubkey, 0xBB, 32);
    req_resp.my_external_ip = 0x0A000002u;
    req_resp.my_external_port = 5002;

    rc = norn_rendezvous_handle_req(&state, &req_resp,
                                     req_resp.my_external_ip,
                                     req_resp.my_external_port,
                                     resp_c, &resp_out);
    assert(rc == 1);
    assert(resp_out.msg_type == NORN_MSG_HOLEPUNCH_RESP);

    norn_rendezvous_cleanup(&state);
    norn_free(init_c);
    norn_free(resp_c);
    printf("  rendezvous coordination ok\n");
}

int main(void) {
    assert(sodium_init() >= 0);
    printf("test_holepunch:\n");
    test_holepunch_req_roundtrip();
    test_holepunch_resp_roundtrip();
    test_probe_roundtrip();
    test_null_handling();
    test_rendezvous_enable();
    test_rendezvous_coordination();
    printf("all holepunch tests passed\n");
    return 0;
}
