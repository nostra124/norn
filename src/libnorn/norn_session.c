/**
 * @file norn_session.c
 * @brief Session management implementation (FEAT-016).
 *
 * This is a stub implementation that establishes the API. Full implementation
 * will be completed in phases:
 *
 * Phase 1: Direct connection (loopback testing)
 * Phase 2: NAT traversal (hole-punch, relay)
 * Phase 3: Async/callbacks
 */

#include "norn_session.h"
#include "norn_suite.h"
#include <stdlib.h>
#include <string.h>

struct norn_session {
    norn_client_t *client;
    const norn_crypto_suite_t *suite;
    unsigned char peer_pubkey[32];
    norn_session_state_t state;
    void *user_data;
    norn_session_callback_t callback;
};

struct norn_stream {
    norn_session_t *session;
    int id;
    int closed;
};

/* Stub: creates session structure, does not connect yet */
norn_session_t *norn_dial(norn_client_t *client,
                          const unsigned char *pubkey,
                          const norn_crypto_suite_t *suite,
                          norn_session_callback_t callback,
                          void *user_data) {
    if (!client || !pubkey) return NULL;
    
    norn_session_t *session = calloc(1, sizeof(*session));
    if (!session) return NULL;
    
    session->client = client;
    session->suite = suite ? suite : norn_suite_sodium();
    memcpy(session->peer_pubkey, pubkey, session->suite->pubkey_len);
    session->state = NORN_SESSION_CONNECTING;
    session->callback = callback;
    session->user_data = user_data;
    
    /* TODO: Implement full dial flow:
     * 1. Resolve endpoint via DHT
     * 2. NAT traversal (direct → hole-punch → relay)
     * 3. Channel handshake
     * 4. Transition to ESTABLISHED
     */
    
    /* Stub: Immediately transition to ESTABLISHED for testing */
    session->state = NORN_SESSION_ESTABLISHED;
    
    if (callback) {
        callback(session, NORN_SESSION_ESTABLISHED, user_data);
    }
    
    return session;
}

/* Stub: listen for inbound */
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
    
    /* TODO: Implement listen:
     * 1. Bind UDP socket
     * 2. Advertise endpoint via DHT
     * 3. Accept incoming connections
     */
    
    return 0;
}

/* Stub: accept inbound session (blocking) */
norn_session_t *norn_accept(norn_client_t *client) {
    if (!client) return NULL;
    
    /* TODO: Implement accept:
     * 1. Wait for incoming connection
     * 2. Perform channel handshake
     * 3. Return established session
     */
    
    return NULL;
}

/* Close session gracefully */
void norn_session_close(norn_session_t *session) {
    if (!session) return;
    
    session->state = NORN_SESSION_CLOSING;
    
    /* TODO: Send close notification to peer */
    
    session->state = NORN_SESSION_CLOSED;
}

/* Free session */
void norn_session_free(norn_session_t *session) {
    if (!session) return;
    
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
    stream->id = 1;  /* TODO: Allocate stream ID */
    stream->closed = 0;
    
    return stream;
}

/* Write to stream */
int norn_stream_write(norn_stream_t *stream, const void *data, size_t len) {
    if (!stream || !data) return -1;
    if (stream->closed) return -1;
    
    /* TODO: Integrate with stream.h / streammux.h */
    (void)len;
    
    return -1;  /* Not implemented */
}

/* Read from stream */
int norn_stream_read(norn_stream_t *stream, void *buf, size_t cap) {
    if (!stream || !buf) return -1;
    if (stream->closed) return -1;
    
    /* TODO: Integrate with stream.h / streammux.h */
    (void)cap;
    
    return -1;  /* Not implemented */
}

/* Close stream */
void norn_stream_close(norn_stream_t *stream) {
    if (!stream) return;
    
    stream->closed = 1;
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
    
    /* TODO: Build endpoint record and publish via norn_put_mutable */
    (void)suite;
    
    return -1;  /* Not implemented */
}

/* Resolve endpoint */
int norn_resolve_endpoint(norn_client_t *client,
                          const unsigned char *pubkey,
                          norn_endpoint_t *endpoint,
                          const norn_crypto_suite_t *suite) {
    if (!client || !pubkey || !endpoint) return -1;
    
    /* TODO: Query DHT via norn_get_mutable and parse endpoint record */
    (void)suite;
    
    return -1;  /* Not implemented */
}