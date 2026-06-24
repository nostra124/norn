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
#include "net.h"
#include "mainline.h"
#include "norn_transaction.h"

/* Internal client state (defined in norn_impl.c) */
struct norn_client {
    net_t net;
    mainline_state_t ml;
    norn_transaction_state_t txn;
    unsigned char self_pub[NORN_PUBKEY_BYTES];
    unsigned char self_sec[NORN_SECRETKEY_BYTES];
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
};

#endif /* NORN_INTERNAL_H */