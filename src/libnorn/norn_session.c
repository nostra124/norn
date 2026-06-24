/**
 * @file norn_session.c
 * @brief Session management implementation (FEAT-016).
 *
 * Phase 1: Direct connection with channel handshake.
 * Phase 2: NAT traversal (hole-punch, relay) - TODO
 * Phase 3: Async/callbacks - TODO
 */

#include "norn_session_internal.h"
#include "norn_suite.h"
#include "channel.h"
#include "streammux.h"
#include "transport.h"
#include "transport_udp.h"
#include "net.h"
#include <stdlib.h>
#include <string.h>
#include <sodium.h>

/* Create session structure */
norn_session_t *norn_session_new(norn_client_t *client,
                                  const norn_crypto_suite_t *suite) {
    if (!client) return NULL;
    
    norn_session_t *session = calloc(1, sizeof(*session));
    if (!session) return NULL;
    
    session->client = client;
    session->suite = suite ? suite : norn_suite_sodium();
    session->state = NORN_SESSION_CONNECTING;
    session->fd = -1;
    
    /* Create stream mux */
    session->mux = streammux_new(NULL, NULL);
    if (!session->mux) {
        free(session);
        return NULL;
    }
    
    return session;
}

/* Set identity keys (called by dial/accept) */
int norn_session_set_identity(norn_session_t *session,
                               const unsigned char *pubkey,
                               const unsigned char *secret) {
    if (!session || !pubkey || !secret) return -1;
    
    memcpy(session->self_pubkey, pubkey, session->suite->pubkey_len);
    memcpy(session->self_secret, secret, session->suite->secret_len);
    return 0;
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

/* Dial direct: connect to known endpoint (for testing) */
norn_session_t *norn_dial_direct(norn_client_t *client,
                                  const norn_direct_endpoint_t *endpoint,
                                  const unsigned char *pubkey,
                                  const norn_crypto_suite_t *suite) {
    if (!client || !endpoint || !pubkey) return NULL;
    
    /* Get our identity from norn_client */
    unsigned char self_pub[32], self_sec[64];
    /* TODO: norn_client_t should expose identity keys */
    /* For now, generate ephemeral identity */
    crypto_sign_keypair(self_pub, self_sec);
    
    norn_session_t *session = norn_session_new(client, suite);
    if (!session) return NULL;
    
    session->is_initiator = 1;
    norn_session_set_identity(session, self_pub, self_sec);
    memcpy(session->peer_pubkey, pubkey, session->suite->pubkey_len);
    
    /* Store peer endpoint */
    session->peer_ip = endpoint->ip;
    session->peer_port = endpoint->port;
    
    /* Generate ephemeral key for channel handshake */
    if (channel_gen_ephemeral(&session->channel) != 0) {
        streammux_free(session->mux);
        free(session);
        return NULL;
    }
    
    /* TODO Phase 1: Create UDP socket and perform handshake
     * Handshake flow (initiator):
     * 1. Create UDP socket
     * 2. Send INIT (channel_hs_build_init)
     * 3. Receive RESP
     * 4. Send CONFIRM (channel_hs_confirm)
     * 5. Mark ESTABLISHED
     * 
     * For now, stub to CONNECTING state.
     * Full implementation requires event loop integration.
     */
    
    session->state = NORN_SESSION_CONNECTING;
    
    return session;
}

/* Accept direct: respond to incoming connection (for testing) */
norn_session_t *norn_accept_direct(norn_client_t *client,
                                    const unsigned char *pubkey,
                                    const unsigned char *secret,
                                    int fd,
                                    const norn_crypto_suite_t *suite) {
    if (!client || !pubkey || !secret || fd < 0) return NULL;
    
    norn_session_t *session = norn_session_new(client, suite);
    if (!session) return NULL;
    
    session->is_initiator = 0;
    norn_session_set_identity(session, pubkey, secret);
    session->fd = fd;
    
    /* Generate ephemeral key */
    if (channel_gen_ephemeral(&session->channel) != 0) {
        streammux_free(session->mux);
        free(session);
        return NULL;
    }
    
    /* TODO Phase 1: Receive INIT, send RESP, receive CONFIRM
     * Handshake flow (responder):
     * 1. Receive INIT from fd
     * 2. Call channel_hs_accept to build RESP
     * 3. Send RESP
     * 4. Receive CONFIRM
     * 5. Call channel_hs_finish to verify
     * 6. Mark ESTABLISHED
     */
    
    session->state = NORN_SESSION_CONNECTING;
    
    return session;
}

/* === Handshake message processing === */

int norn_session_build_init(norn_session_t *session,
                             unsigned char *out, size_t outcap) {
    if (!session || !out) return -1;
    if (!session->is_initiator) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return -1;
    
    /* Build INIT message using channel handshake */
    int len = channel_hs_build_init(&session->channel, session->self_pubkey,
                                     out, outcap);
    return len;
}

int norn_session_accept_init(norn_session_t *session,
                              const unsigned char *init_msg, size_t init_len,
                              unsigned char *out, size_t outcap) {
    if (!session || !init_msg || !out) return -1;
    if (session->is_initiator) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return -1;
    
    /* Process INIT and build RESP */
    unsigned char peer_pubkey[32];
    int len = channel_hs_accept(&session->channel,
                                session->self_pubkey, session->self_secret,
                                init_msg, init_len,
                                peer_pubkey,
                                out, outcap);
    
    if (len < 0) return -1;
    
    /* Store peer identity */
    memcpy(session->peer_pubkey, peer_pubkey, 32);
    
    return len;
}

int norn_session_confirm_resp(norn_session_t *session,
                               const unsigned char *resp_msg, size_t resp_len,
                               unsigned char *out, size_t outcap) {
    if (!session || !resp_msg || !out) return -1;
    if (!session->is_initiator) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return -1;
    
    /* Process RESP and build CONFIRM */
    unsigned char peer_pubkey[32];
    int len = channel_hs_confirm(&session->channel,
                                  session->self_pubkey, session->self_secret,
                                  resp_msg, resp_len,
                                  peer_pubkey,
                                  out, outcap);
    
    if (len < 0) return -1;
    
    /* Store peer identity */
    memcpy(session->peer_pubkey, peer_pubkey, 32);
    
    /* Mark session as established */
    session->state = NORN_SESSION_ESTABLISHED;
    
    return len;
}

int norn_session_finish_confirm(norn_session_t *session,
                                 const unsigned char *confirm_msg, size_t confirm_len) {
    if (!session || !confirm_msg) return -1;
    if (session->is_initiator) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return -1;
    
    /* Verify CONFIRM */
    int ret = channel_hs_finish(&session->channel,
                                 session->peer_pubkey,
                                 confirm_msg, confirm_len);
    
    if (ret != 0) return -1;
    
    /* Mark session as established */
    session->state = NORN_SESSION_ESTABLISHED;
    
    return 0;
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