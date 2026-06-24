/**
 * @file norn_rendezvous.c
 * @brief Rendezvous coordination service implementation (FEAT-017 Phase 3).
 */

#include "norn_rendezvous.h"
#include "norn_internal.h"
#include "crypto.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <time.h>

#define PENDING_TIMEOUT_MS 30000

int norn_rendezvous_init(norn_rendezvous_t *rv) {
    if (!rv) return -1;
    
    rv->pending_cap = 16;
    rv->pending = calloc(rv->pending_cap, sizeof(*rv->pending));
    if (!rv->pending) return -1;
    
    rv->pending_count = 0;
    rv->timeout_ms = PENDING_TIMEOUT_MS;
    
    return 0;
}

void norn_rendezvous_cleanup(norn_rendezvous_t *rv) {
    if (!rv) return;
    
    free(rv->pending);
    rv->pending = NULL;
    rv->pending_count = 0;
    rv->pending_cap = 0;
}

static int find_pending(norn_rendezvous_t *rv,
                        const uint8_t *initiator,
                        const uint8_t *responder) {
    for (int i = 0; i < rv->pending_count; i++) {
        if (memcmp(rv->pending[i].initiator_pubkey, initiator, 32) == 0 &&
            memcmp(rv->pending[i].responder_pubkey, responder, 32) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_pending(norn_rendezvous_t *rv) {
    if (rv->pending_count >= rv->pending_cap) {
        int new_cap = rv->pending_cap * 2;
        norn_pending_req_t *new_pending = realloc(rv->pending,
                                                   new_cap * sizeof(*new_pending));
        if (!new_pending) return -1;
        
        rv->pending = new_pending;
        rv->pending_cap = new_cap;
    }
    
    return rv->pending_count++;
}

int norn_rendezvous_handle_req(norn_rendezvous_t *rv,
                               const norn_holepunch_req_t *req,
                               uint32_t from_ip,
                               uint16_t from_port,
                               norn_client_t *client,
                               norn_holepunch_resp_t *resp_out) {
    if (!rv || !req || !client || !resp_out) return -1;
    (void)from_ip;
    (void)from_port;
    
    uint64_t now = (uint64_t)time(NULL) * 1000;
    
    int idx = find_pending(rv, req->target_pubkey, client->self_pub);
    
    if (idx < 0) {
        idx = add_pending(rv);
        if (idx < 0) return -1;
        
        memset(&rv->pending[idx], 0, sizeof(rv->pending[idx]));
        memcpy(rv->pending[idx].initiator_pubkey, client->self_pub, 32);
        memcpy(rv->pending[idx].responder_pubkey, req->target_pubkey, 32);
        rv->pending[idx].timestamp = now;
    }
    
    if (memcmp(client->self_pub, req->target_pubkey, 32) == 0) {
        if (rv->pending[idx].have_responder) return -1;
        
        memcpy(rv->pending[idx].responder_ephemeral, req->my_ephemeral_pubkey, 32);
        rv->pending[idx].responder_ip = req->my_external_ip;
        rv->pending[idx].responder_port = req->my_external_port;
        rv->pending[idx].have_responder = 1;
    } else {
        if (rv->pending[idx].have_initiator) return -1;
        
        memcpy(rv->pending[idx].initiator_ephemeral, req->my_ephemeral_pubkey, 32);
        rv->pending[idx].initiator_ip = req->my_external_ip;
        rv->pending[idx].initiator_port = req->my_external_port;
        rv->pending[idx].have_initiator = 1;
    }
    
    if (rv->pending[idx].have_initiator && rv->pending[idx].have_responder) {
        resp_out->msg_type = NORN_MSG_HOLEPUNCH_RESP;
        memcpy(resp_out->peer_pubkey, rv->pending[idx].responder_pubkey, 32);
        resp_out->peer_external_ip = rv->pending[idx].responder_ip;
        resp_out->peer_external_port = rv->pending[idx].responder_port;
        memcpy(resp_out->peer_ephemeral_pubkey, rv->pending[idx].responder_ephemeral, 32);
        
        bf_sign(resp_out->signature,
                (const unsigned char *)resp_out,
                offsetof(norn_holepunch_resp_t, signature),
                client->self_sec);
        
        memmove(&rv->pending[idx], &rv->pending[rv->pending_count - 1],
                sizeof(rv->pending[idx]));
        rv->pending_count--;
        
        return 1;
    }
    
    return 0;
}

int norn_send_holepunch_req_async(norn_client_t *client,
                                   const uint8_t *target_pubkey,
                                   const uint8_t *rendezvous_pubkey,
                                   const uint8_t *my_ephemeral,
                                   void (*callback)(norn_client_t *,
                                                    const norn_holepunch_resp_t *,
                                                    void *),
                                   void *user_data) {
    if (!client || !target_pubkey || !rendezvous_pubkey || !my_ephemeral || !callback) {
        return -1;
    }
    
    norn_holepunch_req_t req;
    memset(&req, 0, sizeof(req));
    
    req.msg_type = NORN_MSG_HOLEPUNCH_REQ;
    memcpy(req.target_pubkey, target_pubkey, 32);
    memcpy(req.my_ephemeral_pubkey, my_ephemeral, 32);
    
    if (net_get_external_endpoint(&client->net, &req.my_external_ip, &req.my_external_port) != 0) {
        req.my_external_ip = 0;
        req.my_external_port = 0;
    }
    
    bf_sign(req.signature, (const unsigned char *)&req, 
            offsetof(norn_holepunch_req_t, signature), client->self_sec);
    
    uint8_t buf[NORN_HOLEPUNCH_REQ_LEN];
    if (norn_encode_holepunch_req(&req, buf) != 0) {
        return -1;
    }
    
    (void)user_data;
    
    return -1;
}

int norn_send_probes(norn_client_t *client,
                     uint32_t peer_ip,
                     uint16_t peer_port,
                     int count,
                     int interval_ms) {
    if (!client) return -1;
    if (count <= 0) count = 3;
    if (interval_ms <= 0) interval_ms = 100;
    
    uint8_t probe[1] = {0};
    
    for (int i = 0; i < count; i++) {
        if (net_send(&client->net, probe, sizeof(probe), peer_ip, peer_port) < 0) {
            return -1;
        }
        
        if (i < count - 1) {
            struct timespec ts = {
                .tv_sec = interval_ms / 1000,
                .tv_nsec = (interval_ms % 1000) * 1000000L
            };
            nanosleep(&ts, NULL);
        }
    }
    
    return 0;
}