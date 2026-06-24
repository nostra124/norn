/**
 * @file norn_session_internal.h
 * @brief Internal session structure definition.
 *
 * This header is private to the norn_session implementation.
 * External code should use norn_session.h API only.
 */

#ifndef NORN_SESSION_INTERNAL_H
#define NORN_SESSION_INTERNAL_H

#include "norn_session.h"
#include "channel.h"
#include "streammux.h"

/* Internal session state */
struct norn_session {
    norn_client_t *client;
    const norn_crypto_suite_t *suite;
    
    /* Crypto state */
    channel_t channel;
    unsigned char peer_pubkey[64];  /* Max size for any suite */
    unsigned char self_pubkey[32];   /* Our identity public key */
    unsigned char self_secret[64];   /* Our identity secret key */
    
    /* Stream multiplexing */
    streammux_t *mux;
    
    /* Transport */
    int fd;                           /* UDP socket, -1 if not connected */
    uint32_t peer_ip;                 /* Network byte order */
    uint16_t peer_port;               /* Network byte order */
    
    /* State */
    norn_session_state_t state;
    int is_initiator;
    
    /* Callbacks */
    norn_session_callback_t callback;
    void *user_data;
};

/* Internal stream state */
struct norn_stream {
    norn_session_t *session;
    uint16_t stream_id;
    int closed;
};

/* Internal helper - set identity keys */
int norn_session_set_identity(norn_session_t *session,
                               const unsigned char *pubkey,
                               const unsigned char *secret);

#endif /* NORN_SESSION_INTERNAL_H */