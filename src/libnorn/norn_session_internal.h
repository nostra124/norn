/* SPDX-License-Identifier: MIT */
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
    channel_signer_fn signer;        /* external handshake signer, or NULL (FEAT-028) */
    void *signer_ud;
    
    /* Application-protocol multiplexing (one UDP port, one session): each
     * service id gets its own streammux (independent stream-id space + flow
     * control), created lazily. Inbound app frames carry [kind][service] so the
     * session routes streams/datagrams to the right service without collision. */
    struct svc_mux {
        int active;
        norn_service_t service;
        streammux_t *mux;
        uint16_t next_stream_id;       /* odd initiator / even responder */
        norn_session_t *session;       /* back-pointer for the send callback */
    } svc[NORN_MAX_SERVICES];
    norn_stream_t **streams;           /* open stream wrappers, keyed (service,id) */
    int stream_count;
    int stream_cap;
    
    /* Transport */
    int fd;                           /* UDP socket, -1 if not connected */
    int shared_fd;                    /* 1 if fd is the client's shared listen
                                         socket (inbound session): demuxed by the
                                         listener, not closed on free */
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

    /* Inbound-stream accept handler (FEAT-018: server side) */
    void (*accept_stream_cb)(norn_stream_t *stream, void *user_data);
    void *accept_stream_ud;
};

/* Internal stream state */
struct norn_stream {
    norn_session_t *session;
    norn_service_t service;            /* application protocol this stream carries */
    streammux_t *mux;                  /* the per-service mux it lives in */
    uint16_t stream_id;
    int closed;
    norn_stream_callback_t callback;
    void *user_data;
};

/* Internal - allocate a session (state machine driven by the dial/listen path) */
norn_session_t *norn_session_new(norn_client_t *client,
                                 const norn_crypto_suite_t *suite);

/* Internal helper - set identity keys */
int norn_session_set_identity(norn_session_t *session,
                               const unsigned char *pubkey,
                               const unsigned char *secret);

/* Internal helper - route handshake signing through an external signer (FEAT-028).
 * fn=NULL reverts to signing with self_secret. */
void norn_session_set_signer(norn_session_t *session, channel_signer_fn fn,
                             void *ud);

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

/* Internal - dispatch a UDP datagram to session accept/route. Called from
 * norn_tick's dispatch_response when the DHT and session sockets are unified.
 * Returns 0 if the packet was handled (matched an existing session or was a
 * valid INIT), -1 otherwise (so the caller can fall through to DHT). */
int norn_session_dispatch_udp(norn_client_t *client, const unsigned char *buf,
                                size_t len, uint32_t ip, uint16_t port);

#endif /* NORN_SESSION_INTERNAL_H */