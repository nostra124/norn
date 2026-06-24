/**
 * @file norn_session.c
 * @brief Session management implementation (FEAT-016).
 *
 * Phase 1: Direct connection with channel handshake.
 * Phase 2: NAT traversal (hole-punch, relay) - TODO
 * Phase 3: Async/callbacks - TODO
 */

#include "norn_session.h"
#include "norn_suite.h"
#include "channel.h"
#include "streammux.h"
#include <stdlib.h>
#include <string.h>

/* Internal session state */
struct norn_session {
    norn_client_t *client;
    const norn_crypto_suite_t *suite;
    
    /* Crypto state */
    channel_t channel;
    unsigned char peer_pubkey[64];  /* Max size for any suite */
    
    /* Stream multiplexing */
    streammux_t *mux;
    
    /* State */
    norn_session_state_t state;
    int is_initiator;
    
    /* Transport (Phase 2) */
    /* TODO: transport integration for NAT traversal */
    
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

/* Create session structure */
norn_session_t *norn_session_new(norn_client_t *client,
                                  const norn_crypto_suite_t *suite) {
    if (!client) return NULL;
    
    norn_session_t *session = calloc(1, sizeof(*session));
    if (!session) return NULL;
    
    session->client = client;
    session->suite = suite ? suite : norn_suite_sodium();
    session->state = NORN_SESSION_CONNECTING;
    
    /* Create stream mux */
    session->mux = streammux_new(NULL, NULL);
    if (!session->mux) {
        free(session);
        return NULL;
    }
    
    return session;
}

/* Dial: initiator side */
norn_session_t *norn_dial(norn_client_t *client,
                          const unsigned char *pubkey,
                          const norn_crypto_suite_t *suite,
                          norn_session_callback_t callback,
                          void *user_data) {
    if (!client || !pubkey) return NULL;
    
    norn_session_t *session = norn_session_new(client, suite);
    if (!session) return NULL;
    
    session->is_initiator = 1;
    memcpy(session->peer_pubkey, pubkey, session->suite->pubkey_len);
    session->callback = callback;
    session->user_data = user_data;
    
    /* Generate ephemeral key */
    if (channel_gen_ephemeral(&session->channel) != 0) {
        streammux_free(session->mux);
        free(session);
        return NULL;
    }
    
    /* TODO Phase 1: Build and send INIT via transport
     * TODO Phase 2: Resolve endpoint via DHT
     * TODO Phase 2: NAT traversal (direct → hole-punch → relay)
     */
    
    /* Stub: Immediately transition to ESTABLISHED for testing */
    session->state = NORN_SESSION_ESTABLISHED;
    
    if (callback) {
        callback(session, NORN_SESSION_ESTABLISHED, user_data);
    }
    
    return session;
}

/* Listen for inbound */
int norn_listen(norn_client_t *client,
                uint16_t port,
                const norn_crypto_suite_t *suite,
                norn_accept_callback_t callback,
                void *user_data) {
    (void)port;
    (void)suite;
    (void)callback;
    (void)user_data;
    
    if (!client) return -1;
    
    /* TODO Phase 1: Bind transport and accept incoming handshakes
     * TODO Phase 2: Advertise endpoint via DHT
     */
    
    return 0;
}

/* Accept inbound session (blocking) */
norn_session_t *norn_accept(norn_client_t *client) {
    if (!client) return NULL;
    
    /* TODO Phase 1: Wait for incoming handshake
     * TODO Phase 2: Handle multiple inbound sessions
     */
    
    return NULL;
}

/* Close session gracefully */
void norn_session_close(norn_session_t *session) {
    if (!session) return;
    
    session->state = NORN_SESSION_CLOSING;
    
    /* TODO: Send close notification */
    
    if (session->mux) {
        streammux_free(session->mux);
        session->mux = NULL;
    }
    
    session->state = NORN_SESSION_CLOSED;
}

/* Free session */
void norn_session_free(norn_session_t *session) {
    if (!session) return;
    
    if (session->mux) {
        streammux_free(session->mux);
    }
    
    free(session);
}

/* Get session state */
norn_session_state_t norn_session_get_state(const norn_session_t *session) {
    if (!session) return NORN_SESSION_CLOSED;
    return session->state;
}

/* Get peer pubkey */
int norn_session_get_peer(const norn_session_t *session, unsigned char *pubkey) {
    if (!session || !pubkey) return -1;
    if (session->state != NORN_SESSION_ESTABLISHED) return -1;
    
    memcpy(pubkey, session->peer_pubkey, session->suite->pubkey_len);
    return 0;
}

/* Get suite */
const norn_crypto_suite_t *norn_session_get_suite(const norn_session_t *session) {
    if (!session) return NULL;
    return session->suite;
}

/* Open stream */
norn_stream_t *norn_stream_open(norn_session_t *session) {
    if (!session) return NULL;
    if (session->state != NORN_SESSION_ESTABLISHED) return NULL;
    
    norn_stream_t *stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    
    stream->session = session;
    stream->stream_id = 1;  /* TODO: allocate from mux */
    stream->closed = 0;
    
    /* TODO: Open stream in mux */
    
    return stream;
}

/* Write to stream */
int norn_stream_write(norn_stream_t *stream, const void *data, size_t len) {
    if (!stream || !data) return -1;
    if (stream->closed) return -1;
    if (!stream->session || !stream->session->mux) return -1;
    
    /* TODO: Integrate with streammux */
    (void)len;
    
    return -1;  /* Not implemented */
}

/* Read from stream */
int norn_stream_read(norn_stream_t *stream, void *buf, size_t cap) {
    if (!stream || !buf) return -1;
    if (stream->closed) return -1;
    if (!stream->session || !stream->session->mux) return -1;
    
    /* TODO: Integrate with streammux */
    (void)cap;
    
    return -1;  /* Not implemented */
}

/* Close stream */
void norn_stream_close(norn_stream_t *stream) {
    if (!stream) return;
    
    stream->closed = 1;
    
    /* TODO: Send FIN via mux */
}

/* Free stream */
void norn_stream_free(norn_stream_t *stream) {
    if (!stream) return;
    
    free(stream);
}

/* Announce endpoint */
int norn_announce_endpoint(norn_client_t *client,
                           const norn_endpoint_t *endpoint,
                           const unsigned char *secret,
                           const norn_crypto_suite_t *suite) {
    if (!client || !endpoint || !secret) return -1;
    
    /* TODO Phase 2: Build endpoint record and publish via norn_put_mutable */
    (void)suite;
    
    return -1;  /* Not implemented */
}

/* Resolve endpoint */
int norn_resolve_endpoint(norn_client_t *client,
                          const unsigned char *pubkey,
                          norn_endpoint_t *endpoint,
                          const norn_crypto_suite_t *suite) {
    if (!client || !pubkey || !endpoint) return -1;
    
    /* TODO Phase 2: Query DHT via norn_get_mutable and parse endpoint record */
    (void)suite;
    
    return -1;  /* Not implemented */
}