/**
 * @file norn_internal.h
 * @brief Internal norn client structure definition.
 *
 * This header is private to the norn implementation.
 * External code should use norn.h API only.
 */

#ifndef NORN_INTERNAL_H
#define NORN_INTERNAL_H

#include "norn.h"
#include "norn_session.h"
#include "norn_endpoint_cache.h"
#include "norn_nat.h"
#include "norn_rendezvous.h"
#include "norn_relay.h"
#include "norn_bonjour.h"
#include "net.h"
#include "mainline.h"
#include "norn_transaction.h"

/* Forward declarations */
#ifndef NORN_SESSION_T_DEFINED
#define NORN_SESSION_T_DEFINED
typedef struct norn_session norn_session_t;
#endif

/* Dial context forward declaration (defined in norn_session.c) */
struct dial_context;

/* Internal client state (defined in norn_impl.c) */
struct norn_client {
    net_t net;
    mainline_state_t ml;
    norn_transaction_state_t txn;
    unsigned char self_pub[NORN_PUBKEY_BYTES];
    unsigned char self_sec[NORN_SECRETKEY_BYTES];
    norn_sign_fn signer;      /* external handshake signer, or NULL (FEAT-028) */
    void *signer_ud;
    norn_config_t cfg;
    int initialized;
    
    /* Session tracking (FEAT-016) */
    norn_session_t **sessions;
    int session_count;
    int session_cap;
    
    /* Listener (FEAT-016) */
    int listen_fd;
    norn_accept_callback_t listen_callback;
    void *listen_user_data;
    const norn_crypto_suite_t *listen_suite;
    
    /* Endpoint cache (FEAT-017) */
    norn_endpoint_cache_t endpoint_cache;
    
    /* External IP discovery (FEAT-017) */
    uint32_t external_ip;             /* Discovered external IP */
    uint16_t external_port;           /* Discovered external port */
    int have_external_addr;           /* 1 if external IP/port are valid */
    
    /* Rendezvous service (FEAT-017) */
    norn_rendezvous_t rv;             /* Rendezvous coordination state */
    int rendezvous_enabled;           /* 1 if acting as rendezvous */
    void *rendezvous_callback;         /* Callback for coordination */
    void *rendezvous_user_data;       /* User data for callback */
    
    /* Relay service (FEAT-017) */
    norn_relay_t relay;               /* Relay forwarding state */

    /* Bonjour/mDNS service announcement and discovery (FEAT-036) */
    norn_bonjour_t *bonjour;
    
    /* Hole punch pending requests (FEAT-023) */
    struct {
        uint8_t ephemeral_pubkey[32];
        norn_holepunch_callback_t callback;
        void *user_data;  /* dial_context_t* from norn_session.c */
        uint64_t timestamp;
        int active;
    } holepunch_pending[8];
    int holepunch_pending_count;

    /* Application-protocol service registry (FEAT-033): inbound stream/datagram
     * handlers keyed by service id, applied to every session. */
    struct {
        norn_service_t service;
        void (*cb)(norn_stream_t *stream, void *ud);
        void *ud;
    } stream_svcs[NORN_MAX_SERVICES];
    int stream_svc_count;
    struct {
        norn_service_t service;
        norn_datagram_cb_t cb;
        void *ud;
    } dgram_svcs[NORN_MAX_SERVICES];
    int dgram_svc_count;
};

/* Internal lookups used by the session router (defined in norn_impl.c). Return
 * 0 and fill the cb+ud outputs if a handler is registered for `service`,
 * else -1. */
int norn_client_stream_svc(norn_client_t *client, norn_service_t service,
                           void (**cb)(norn_stream_t *, void *), void **ud);
int norn_client_dgram_svc(norn_client_t *client, norn_service_t service,
                          norn_datagram_cb_t *cb, void **ud);

/* FEAT-023: Internal helper for probe-to-session transition */
int norn_session_from_probe(norn_client_t *client,
                             void *dial_ctx,
                             uint32_t from_ip,
                             uint16_t from_port);

#endif /* NORN_INTERNAL_H */