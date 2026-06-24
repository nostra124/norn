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

/* Internal handshake states (for async state machine) */
typedef enum {
    HS_NONE,              /* No handshake in progress */
    HS_INIT_SENT,         /* INIT sent, waiting for RESP */
    HS_RESP_SENT,         /* RESP sent, waiting for CONFIRM */
    HS_CONFIRM_SENT,      /* CONFIRM sent, waiting for ACK (optional) */
    HS_ESTABLISHED        /* Handshake complete */
} norn_hs_state_t;

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
    
    /* State machine */
    norn_session_state_t state;
    norn_hs_state_t hs_state;         /* Internal handshake state */
    int is_initiator;
    
    /* Retransmission */
    unsigned char last_msg[256];      /* Last sent message (for retransmit) */
    size_t last_msg_len;
    uint32_t last_send_time;          /* Time of last send (ms) */
    int retry_count;
    
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

/* Internal - process handshake packet (called from norn_tick) */
int norn_session_process_packet(norn_session_t *session,
                                 const unsigned char *data,
                                 size_t len,
                                 uint32_t from_ip,
                                 uint16_t from_port);

/* Internal - send pending handshake message (called from norn_tick) */
int norn_session_send_pending(norn_session_t *session);

/* Internal - check handshake timeout (called from norn_tick) */
int norn_session_check_timeout(norn_session_t *session, uint32_t now_ms);

#endif /* NORN_SESSION_INTERNAL_H */