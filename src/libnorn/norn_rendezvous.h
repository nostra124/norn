/* SPDX-License-Identifier: MIT */
/**
 * @file norn_rendezvous.h
 * @brief Rendezvous coordination service (FEAT-017 Phase 3).
 *
 * Provides rendezvous service for NAT traversal:
 * - Coordinates hole punch between peers
 * - Relays connection information
 * - Manages simultaneous probe timing
 *
 * Wire Protocol:
 * - Initiator sends HolePunchRequest to rendezvous
 * - Rendezvous forwards to responder
 * - Responder replies with HolePunchResponse
 * - Rendezvous forwards to initiator
 * - Both peers send simultaneous UDP probes
 */

#ifndef NORN_RENDEZVOUS_H
#define NORN_RENDEZVOUS_H

#include "norn.h"
#include "norn_nat.h"

/**
 * @brief Rendezvous pending request state.
 *
 * Tracks a pending hole punch coordination request.
 * When both peers have sent requests, rendezvous can respond.
 */
typedef struct {
    uint8_t initiator_pubkey[32];
    uint8_t responder_pubkey[32];
    uint8_t initiator_ephemeral[32];
    uint8_t responder_ephemeral[32];
    uint32_t initiator_ip;
    uint16_t initiator_port;
    uint32_t responder_ip;
    uint16_t responder_port;
    int have_initiator;
    int have_responder;
    uint64_t timestamp;
} norn_pending_req_t;

/**
 * @brief Rendezvous service state.
 */
typedef struct {
    norn_pending_req_t *pending;
    int pending_count;
    int pending_cap;
    uint64_t timeout_ms;
} norn_rendezvous_t;

/**
 * @brief Initialize rendezvous service.
 *
 * @param rv Rendezvous state
 * @return 0 on success, -1 on error
 */
int norn_rendezvous_init(norn_rendezvous_t *rv);

/**
 * @brief Cleanup rendezvous service.
 *
 * @param rv Rendezvous state
 */
void norn_rendezvous_cleanup(norn_rendezvous_t *rv);

/**
 * @brief Process incoming HolePunchRequest.
 *
 * Called when acting as rendezvous for two peers.
 * Stores request until both peers have sent requests.
 *
 * @param rv Rendezvous state
 * @param req Incoming request
 * @param from_ip Sender's IP
 * @param from_port Sender's port
 * @param client Norn client (for signing responses)
 * @param resp_out Output response (if ready)
 * @return 1 if response ready, 0 if waiting for peer, -1 on error
 */
int norn_rendezvous_handle_req(norn_rendezvous_t *rv,
                               const norn_holepunch_req_t *req,
                               uint32_t from_ip,
                               uint16_t from_port,
                               norn_client_t *client,
                               norn_holepunch_resp_t *resp_out);

/**
 * @brief Callback for hole punch response.
 *
 * @param client Norn client
 * @param resp Response from rendezvous
 * @param user_data User data from request
 */
typedef void (*norn_holepunch_callback_t)(norn_client_t *client,
                                          const norn_holepunch_resp_t *resp,
                                          void *user_data);

/**
 * @brief Send HolePunchRequest to rendezvous.
 *
 * Called when we want to connect to a peer through rendezvous.
 *
 * @param client Norn client
 * @param target_pubkey Peer to connect to
 * @param rendezvous_ip Rendezvous peer IP (network byte order)
 * @param rendezvous_port Rendezvous peer port (network byte order)
 * @param my_ephemeral Our ephemeral key for this session
 * @param callback Called when we receive HolePunchResponse
 * @param user_data User data for callback
 * @return 0 on success, -1 on error
 */
int norn_send_holepunch_req_async(norn_client_t *client,
                                   const uint8_t *target_pubkey,
                                   uint32_t rendezvous_ip,
                                   uint16_t rendezvous_port,
                                   const uint8_t *my_ephemeral,
                                   norn_holepunch_callback_t callback,
                                   void *user_data);

/**
 * @brief Send simultaneous UDP probes.
 *
 * After receiving HolePunchResponse, send probes to peer's external IP.
 *
 * @param client Norn client
 * @param ephemeral_pubkey Our ephemeral pubkey for this session
 * @param peer_ip Peer's external IP
 * @param peer_port Peer's external port
 * @param count Number of probes to send
 * @param interval_ms Interval between probes
 * @return 0 on success, -1 on error
 */
int norn_send_probes(norn_client_t *client,
                     const uint8_t ephemeral_pubkey[32],
                     uint32_t peer_ip,
                     uint16_t peer_port,
                     int count,
                     int interval_ms);

#endif /* NORN_RENDEZVOUS_H */