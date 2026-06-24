/**
 * @file norn_session.c
 * @brief Async session management implementation (FEAT-016).
 *
 * Fully async, non-blocking session management with event loop integration.
 * No blocking I/O - all operations callback-based.
 */

#include "norn.h"
#include "norn_internal.h"
#include "norn_session_internal.h"
#include "norn_suite.h"
#include "norn_transaction.h"
#include "channel.h"
#include "streammux.h"
#include "mainline.h"
#include "bep44.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sodium.h>

/* === Internal Session Management === */

norn_session_t *norn_session_new(norn_client_t *client,
                                  const norn_crypto_suite_t *suite) {
    if (!client) return NULL;
    
    norn_session_t *session = calloc(1, sizeof(*session));
    if (!session) return NULL;
    
    session->client = client;
    session->suite = suite ? suite : norn_suite_sodium();
    session->state = NORN_SESSION_CONNECTING;
    session->fd = -1;
    
    session->mux = streammux_new(NULL, NULL);
    if (!session->mux) {
        free(session);
        return NULL;
    }
    
    return session;
}

int norn_session_set_identity(norn_session_t *session,
                              const unsigned char *pubkey,
                              const unsigned char *secret) {
    if (!session || !pubkey || !secret) return -1;
    
    memcpy(session->self_pubkey, pubkey, session->suite->pubkey_len);
    memcpy(session->self_secret, secret, session->suite->secret_len);
    return 0;
}

/* === Async Dial API === */

/**
 * Internal state machine for dial process.
 */
typedef enum {
    DIAL_RESOLVING,      /* Resolving endpoint from DHT */
    DIAL_CONNECTING,     /* Attempting direct connection */
    DIAL_HOLEPUNCH,      /* Attempting hole punch (Phase 3) */
    DIAL_RELAY,          /* Attempting relay (Phase 4) */
    DIAL_ESTABLISHED,    /* Session established */
    DIAL_FAILED          /* All attempts failed */
} dial_state_t;

/**
 * Internal dial context (tracks state across async operations).
 */
typedef struct {
    norn_client_t *client;
    unsigned char peer_pubkey[64];
    const norn_crypto_suite_t *suite;
    norn_session_callback_t callback;
    void *user_data;
    dial_state_t state;
    norn_session_t *session;
    uint32_t resolve_txn_id;  /* DHT transaction ID */
} dial_context_t;

/**
 * Callback for endpoint resolution.
 */
static void on_endpoint_resolved(const norn_endpoint_t *endpoint, void *user_data) {
    dial_context_t *ctx = (dial_context_t *)user_data;
    if (!ctx) return;
    
    if (!endpoint) {
        /* Resolution failed - try hole punch or relay */
        ctx->state = DIAL_FAILED;
        if (ctx->callback) {
            ctx->callback(NULL, NORN_SESSION_CLOSED, ctx->user_data);
        }
        free(ctx);
        return;
    }
    
    /* Check if endpoint has public IP (direct connection possible) */
    if (endpoint->ip != 0 && (endpoint->caps & NORN_EP_CAP_DIRECT)) {
        /* Try direct connection */
        norn_direct_endpoint_t direct_ep = {
            .ip = endpoint->ip,
            .port = endpoint->port
        };
        
        int ret = norn_dial_direct_async(ctx->client, &direct_ep,
                                         ctx->peer_pubkey, ctx->suite,
                                         ctx->callback, ctx->user_data);
        if (ret == 0) {
            ctx->state = DIAL_CONNECTING;
            /* Direct dial started - context will be freed by session */
            free(ctx);
            return;
        }
    }
    
    /* Direct not available - fall back to hole punch (Phase 3) */
    if (endpoint->caps & NORN_EP_CAP_RENDEZVOUS) {
        /* Endpoint can act as rendezvous/introducer for hole punch */
        ctx->state = DIAL_HOLEPUNCH;
        
        /* FEAT-023: Hole punch integration - wire up the connection
         * For now, we have the wire protocol complete but the full
         * integration requires:
         * 1. Generate ephemeral key for this session
         * 2. Send HolePunchRequest via rendezvous
         * 3. Handle HolePunchResponse callback
         * 4. Send simultaneous probes
         * 5. Establish session
         *
         * This is tracked in .repo/project/issues/FEAT-023-HOLEPUNCH-INTEGRATION.md
         */
        
        /* For now, fall through to relay if available */
    }
    
    /* Hole punch not available or failed - fall back to relay (Phase 4) */
    if (endpoint->caps & NORN_EP_CAP_RELAY) {
        /* FEAT-022: Multi-hop relay - use relay path from endpoint->payload
         * This requires:
         * 1. Parse relay hints from endpoint->payload
         * 2. Discover relay path
         * 3. Send RelayCreate through chain
         * 4. Wait for RelayAccept
         *
         * Tracked in .repo/project/issues/FEAT-022-MULTIPATH-RELAY.md
         */
        ctx->state = DIAL_RELAY;
    }
    
    /* No connection method available - all methods failed or not implemented */
    if (ctx->state != DIAL_HOLEPUNCH && ctx->state != DIAL_RELAY) {
        /* Neither rendezvous nor relay available */
        ctx->state = DIAL_FAILED;
    } else {
        /* Connection method selected but not yet fully implemented */
        ctx->state = DIAL_FAILED;
    }
    
    if (ctx->callback) {
        ctx->callback(NULL, NORN_SESSION_CLOSED, ctx->user_data);
    }
    free(ctx);
}

int norn_dial_async(norn_client_t *client,
                    const unsigned char *pubkey,
                    const norn_crypto_suite_t *suite,
                    norn_session_callback_t callback,
                    void *user_data) {
    if (!client || !pubkey) return -1;
    
    suite = suite ? suite : norn_suite_sodium();
    
    /* Check endpoint cache first */
    const norn_endpoint_t *cached = norn_endpoint_cache_lookup(&client->endpoint_cache, pubkey);
    if (cached) {
        /* Endpoint cached - try direct connection */
        if (cached->ip != 0 && (cached->caps & NORN_EP_CAP_DIRECT)) {
            norn_direct_endpoint_t direct_ep = {
                .ip = cached->ip,
                .port = cached->port
            };
            return norn_dial_direct_async(client, &direct_ep, pubkey, suite, callback, user_data);
        }
    }
    
    /* Create dial context to track state */
    dial_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return -1;
    
    ctx->client = client;
    memcpy(ctx->peer_pubkey, pubkey, suite->pubkey_len);
    ctx->suite = suite;
    ctx->callback = callback;
    ctx->user_data = user_data;
    ctx->state = DIAL_RESOLVING;
    
    /* Resolve endpoint from DHT */
    int ret = norn_resolve_endpoint_async(client, pubkey, suite, on_endpoint_resolved, ctx);
    if (ret != 0) {
        free(ctx);
        return -1;
    }
    
    return 0;
}

int norn_dial_direct_async(norn_client_t *client,
                           const norn_direct_endpoint_t *endpoint,
                           const unsigned char *pubkey,
                           const norn_crypto_suite_t *suite,
                           norn_session_callback_t callback,
                           void *user_data) {
    if (!client || !endpoint || !pubkey) return -1;
    
    /* Get identity from client */
    unsigned char self_pub[32], self_sec[64];
    crypto_sign_keypair(self_pub, self_sec);
    
    norn_session_t *session = norn_session_new(client, suite);
    if (!session) return -1;
    
    session->is_initiator = 1;
    norn_session_set_identity(session, self_pub, self_sec);
    memcpy(session->peer_pubkey, pubkey, session->suite->pubkey_len);
    
    session->peer_ip = endpoint->ip;
    session->peer_port = endpoint->port;
    
    if (channel_gen_ephemeral(&session->channel) != 0) {
        streammux_free(session->mux);
        free(session);
        return -1;
    }
    
    /* Create UDP socket */
    session->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (session->fd < 0) {
        streammux_free(session->mux);
        free(session);
        return -1;
    }
    
    /* Set non-blocking */
    int flags = fcntl(session->fd, F_GETFL, 0);
    fcntl(session->fd, F_SETFL, flags | O_NONBLOCK);
    
    /* Connect UDP socket */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = endpoint->ip;
    addr.sin_port = endpoint->port;
    
    if (connect(session->fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            close(session->fd);
            streammux_free(session->mux);
            free(session);
            return -1;
        }
    }
    
    /* Register with client */
    if (norn_client_add_session(client, session) != 0) {
        close(session->fd);
        streammux_free(session->mux);
        free(session);
        return -1;
    }
    
    session->callback = callback;
    session->user_data = user_data;
    session->state = NORN_SESSION_CONNECTING;
    
    /* Phase 1: Initiate handshake when norn_tick() is called */
    
    return 0;
}

/* === Async Listen API === */

int norn_listen_async(norn_client_t *client,
                      uint16_t port,
                      const norn_crypto_suite_t *suite,
                      norn_accept_callback_t callback,
                      void *user_data) {
    if (!client || !callback) return -1;
    
    /* Bind UDP socket */
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = port;
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    
    /* Set non-blocking */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    /* Store listener in client */
    client->listen_fd = fd;
    client->listen_callback = callback;
    client->listen_user_data = user_data;
    client->listen_suite = suite ? suite : norn_suite_sodium();
    
    return 0;
}

/* === Async Close API === */

int norn_session_close_async(norn_session_t *session,
                             norn_session_callback_t callback,
                             void *user_data) {
    if (!session) return -1;
    
    session->state = NORN_SESSION_CLOSING;
    session->callback = callback;
    session->user_data = user_data;
    
    /* TODO: Send close notification to peer */
    
    /* Close socket */
    if (session->fd >= 0) {
        close(session->fd);
        session->fd = -1;
    }
    
    /* Invoke callback */
    session->state = NORN_SESSION_CLOSED;
    if (callback) {
        callback(session, NORN_SESSION_CLOSED, user_data);
    }
    
    return 0;
}

/* === Internal State Machine === */

int norn_session_process_packet(norn_session_t *session,
                                 const unsigned char *data,
                                 size_t len,
                                 uint32_t from_ip,
                                 uint16_t from_port) {
    if (!session || !data || len == 0) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return -1;
    
    /* Verify packet is from expected peer */
    if (session->peer_ip != 0 && session->peer_ip != from_ip) return -1;
    if (session->peer_port != 0 && session->peer_port != from_port) return -1;
    
    /* Process based on handshake state */
    if (session->is_initiator) {
        /* Initiator expects RESP */
        if (session->hs_state != HS_INIT_SENT) return -1;
        
        unsigned char confirm_msg[CHANNEL_CONFIRM_LEN];
        int confirm_len = norn_session_confirm_resp(session, data, len,
                                                     confirm_msg, sizeof(confirm_msg));
        if (confirm_len < 0) return -1;
        
        /* Send CONFIRM */
        ssize_t sent = send(session->fd, confirm_msg, confirm_len, 0);
        if (sent != confirm_len) return -1;
        
        /* Session established */
        session->hs_state = HS_ESTABLISHED;
        session->state = NORN_SESSION_ESTABLISHED;
        
        /* Invoke callback */
        if (session->callback) {
            session->callback(session, NORN_SESSION_ESTABLISHED, session->user_data);
        }
        
        return 0;
    } else {
        /* Responder expects INIT then CONFIRM */
        if (session->hs_state == HS_NONE) {
            /* Receive INIT, send RESP */
            unsigned char resp_msg[CHANNEL_RESP_LEN];
            int resp_len = norn_session_accept_init(session, data, len,
                                                     resp_msg, sizeof(resp_msg));
            if (resp_len < 0) return -1;
            
            /* Send RESP */
            ssize_t sent = send(session->fd, resp_msg, resp_len, 0);
            if (sent != resp_len) return -1;
            
            session->hs_state = HS_RESP_SENT;
            return 0;
        } else if (session->hs_state == HS_RESP_SENT) {
            /* Receive CONFIRM */
            int ret = norn_session_finish_confirm(session, data, len);
            if (ret != 0) return -1;
            
            /* Session established */
            session->hs_state = HS_ESTABLISHED;
            session->state = NORN_SESSION_ESTABLISHED;
            
            /* Invoke callback */
            if (session->callback) {
                session->callback(session, NORN_SESSION_ESTABLISHED, session->user_data);
            }
            
            return 0;
        }
    }
    
    return -1;
}

int norn_session_send_pending(norn_session_t *session) {
    if (!session) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return 0;
    if (session->fd < 0) return -1;
    
    /* Send pending handshake message (for initiator) */
    if (session->is_initiator && session->hs_state == HS_NONE) {
        /* Build and send INIT */
        unsigned char init_msg[CHANNEL_INIT_LEN];
        int init_len = norn_session_build_init(session, init_msg, sizeof(init_msg));
        if (init_len < 0) return -1;
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = session->peer_ip;
        addr.sin_port = session->peer_port;
        
        ssize_t sent = sendto(session->fd, init_msg, init_len, 0,
                              (struct sockaddr *)&addr, sizeof(addr));
        if (sent != init_len) return -1;
        
        session->hs_state = HS_INIT_SENT;
        memcpy(session->last_msg, init_msg, init_len);
        session->last_msg_len = init_len;
        session->retry_count = 0;
        
        return 0;
    }
    
    return 0;
}

int norn_session_check_timeout(norn_session_t *session, uint32_t now_ms) {
    if (!session) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return 0;
    
    /* TODO: Implement timeout with retransmit */
    /* For now, just return success */
    (void)now_ms;
    return 0;
}

/* === Event Loop Integration === */

int norn_client_add_session(norn_client_t *client, norn_session_t *session) {
    if (!client || !session) return -1;
    
    /* Grow array if needed */
    if (client->session_count >= client->session_cap) {
        int new_cap = client->session_cap ? client->session_cap * 2 : 8;
        norn_session_t **new_sessions = realloc(client->sessions,
                                                 new_cap * sizeof(*new_sessions));
        if (!new_sessions) return -1;
        
        client->sessions = new_sessions;
        client->session_cap = new_cap;
    }
    
    client->sessions[client->session_count++] = session;
    return 0;
}

int norn_client_remove_session(norn_client_t *client, norn_session_t *session) {
    if (!client || !session) return -1;
    
    for (int i = 0; i < client->session_count; i++) {
        if (client->sessions[i] == session) {
            client->sessions[i] = client->sessions[--client->session_count];
            return 0;
        }
    }
    
    return -1;
}

int norn_client_tick_sessions(norn_client_t *client) {
    if (!client) return -1;
    
    int processed = 0;
    uint32_t now_ms = 0; /* TODO: Get actual time */
    
    /* Process each registered session */
    for (int i = 0; i < client->session_count; i++) {
        norn_session_t *session = client->sessions[i];
        
        /* Skip closed sessions */
        if (session->state == NORN_SESSION_CLOSED) continue;
        
        /* Send pending handshake messages */
        if (session->state == NORN_SESSION_CONNECTING) {
            norn_session_send_pending(session);
        }
        
        /* Check timeouts */
        norn_session_check_timeout(session, now_ms);
        
        /* Process incoming packets if fd is set */
        if (session->fd >= 0) {
            unsigned char buf[4096];
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            
            ssize_t len = recvfrom(session->fd, buf, sizeof(buf), MSG_DONTWAIT,
                                   (struct sockaddr *)&from, &fromlen);
            
            if (len > 0) {
                norn_session_process_packet(session, buf, len,
                                             from.sin_addr.s_addr, from.sin_port);
                processed++;
            }
        }
    }
    
    /* Process listener socket */
    if (client->listen_fd >= 0) {
        unsigned char buf[4096];
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        
        ssize_t len = recvfrom(client->listen_fd, buf, sizeof(buf), MSG_DONTWAIT,
                               (struct sockaddr *)&from, &fromlen);
        
        if (len > 0) {
            /* TODO Phase 1: Handle incoming handshake */
            /* For now, would create new session and call norn_session_process_packet */
            processed++;
        }
    }
    
    return processed;
}

int norn_get_session_fds(norn_client_t *client,
                         int *fds,
                         int *events,
                         int max_fds) {
    if (!client || !fds || !events) return -1;
    
    int count = 0;
    
    /* Add listener socket */
    if (client->listen_fd >= 0 && count < max_fds) {
        fds[count] = client->listen_fd;
        events[count] = POLLIN;
        count++;
    }
    
    /* Add session sockets */
    for (int i = 0; i < client->session_count && count < max_fds; i++) {
        norn_session_t *session = client->sessions[i];
        
        if (session->fd >= 0 && session->state != NORN_SESSION_CLOSED) {
            fds[count] = session->fd;
            events[count] = POLLIN;
            count++;
        }
    }
    
    return count;
}

/* === Session Queries === */

norn_session_state_t norn_session_get_state(const norn_session_t *session) {
    return session ? session->state : NORN_SESSION_CLOSED;
}

int norn_session_get_peer(const norn_session_t *session, unsigned char *pubkey) {
    if (!session || !pubkey) return -1;
    if (session->state != NORN_SESSION_ESTABLISHED) return -1;
    
    memcpy(pubkey, session->peer_pubkey, session->suite->pubkey_len);
    return 0;
}

const norn_crypto_suite_t *norn_session_get_suite(const norn_session_t *session) {
    return session ? session->suite : NULL;
}

int norn_session_get_fd(const norn_session_t *session) {
    return session ? session->fd : -1;
}

/* === Resource Management === */

void norn_session_free(norn_session_t *session) {
    if (!session) return;
    
    if (session->fd >= 0) {
        close(session->fd);
    }
    
    if (session->mux) {
        streammux_free(session->mux);
    }
    
    /* Remove from client's session list */
    if (session->client) {
        norn_client_remove_session(session->client, session);
    }
    
    free(session);
}

/* === Blocking Handshake (Deprecated, testing only) === */
/* Implemented in norn_session_udp.c */

/* === Low-Level Handshake (Internal Use) === */

int norn_session_build_init(norn_session_t *session,
                            unsigned char *out,
                            size_t outcap) {
    if (!session || !out) return -1;
    if (!session->is_initiator) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return -1;
    
    return channel_hs_build_init(&session->channel, session->self_pubkey, out, outcap);
}

int norn_session_accept_init(norn_session_t *session,
                             const unsigned char *init_msg,
                             size_t init_len,
                             unsigned char *out,
                             size_t outcap) {
    if (!session || !init_msg || !out) return -1;
    if (session->is_initiator) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return -1;
    
    unsigned char peer_pubkey[32];
    int len = channel_hs_accept(&session->channel,
                                session->self_pubkey, session->self_secret,
                                init_msg, init_len,
                                peer_pubkey,
                                out, outcap);
    
    if (len < 0) return -1;
    
    memcpy(session->peer_pubkey, peer_pubkey, 32);
    return len;
}

int norn_session_confirm_resp(norn_session_t *session,
                              const unsigned char *resp_msg,
                              size_t resp_len,
                              unsigned char *out,
                              size_t outcap) {
    if (!session || !resp_msg || !out) return -1;
    if (!session->is_initiator) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return -1;
    
    unsigned char peer_pubkey[32];
    int len = channel_hs_confirm(&session->channel,
                                 session->self_pubkey, session->self_secret,
                                 resp_msg, resp_len,
                                 peer_pubkey,
                                 out, outcap);
    
    if (len < 0) return -1;
    
    memcpy(session->peer_pubkey, peer_pubkey, 32);
    session->state = NORN_SESSION_ESTABLISHED;
    return len;
}

int norn_session_finish_confirm(norn_session_t *session,
                                const unsigned char *confirm_msg,
                                size_t confirm_len) {
    if (!session || !confirm_msg) return -1;
    if (session->is_initiator) return -1;
    if (session->state != NORN_SESSION_CONNECTING) return -1;
    
    int ret = channel_hs_finish(&session->channel,
                                session->peer_pubkey,
                                confirm_msg, confirm_len);
    
    if (ret != 0) return -1;
    
    session->state = NORN_SESSION_ESTABLISHED;
    return 0;
}

/* === Endpoint Discovery (Async) === */

/**
 * @brief Encode endpoint for DHT storage
 *
 * Format:
 * - uint16_t caps (2 bytes)
 * - uint16_t payload_len (2 bytes)
 * - payload (payload_len bytes)
 */
static int encode_endpoint(const norn_endpoint_t *ep,
                           unsigned char *out,
                           size_t outcap) {
    if (!ep || !out || outcap < 4 + ep->payload_len) return -1;
    
    size_t off = 0;
    
    /* Capabilities */
    out[off++] = (ep->caps >> 8) & 0xFF;
    out[off++] = ep->caps & 0xFF;
    
    /* Payload length */
    out[off++] = (ep->payload_len >> 8) & 0xFF;
    out[off++] = ep->payload_len & 0xFF;
    
    /* Payload */
    if (ep->payload_len > 0) {
        memcpy(out + off, ep->payload, ep->payload_len);
        off += ep->payload_len;
    }
    
    return (int)off;
}

/**
 * @brief Decode endpoint from DHT storage
 */
static int decode_endpoint(norn_endpoint_t *ep,
                           const unsigned char *data,
                           size_t len) __attribute__((unused));
static int decode_endpoint(norn_endpoint_t *ep,
                           const unsigned char *data,
                           size_t len) {
    if (!ep || !data || len < 4) return -1;
    
    size_t off = 0;
    
    /* Capabilities */
    ep->caps = (data[off] << 8) | data[off + 1];
    off += 2;
    
    /* Payload length */
    ep->payload_len = (data[off] << 8) | data[off + 1];
    off += 2;
    
    /* Verify length */
    if (off + ep->payload_len > len) return -1;
    if (ep->payload_len > sizeof(ep->payload)) return -1;
    
    /* Payload */
    if (ep->payload_len > 0) {
        memcpy(ep->payload, data + off, ep->payload_len);
    }
    
    return 0;
}

int norn_announce_endpoint_async(norn_client_t *client,
                                const norn_endpoint_t *endpoint,
                                const unsigned char *secret,
                                const norn_crypto_suite_t *suite,
                                void *callback,
                                void *user_data) {
    if (!client || !endpoint || !secret) return -1;
    if (!client->initialized) return -1;
    
    /* Encode endpoint */
    unsigned char value[1024];
    int value_len = encode_endpoint(endpoint, value, sizeof(value));
    if (value_len < 0) return -1;
    
    /* Compute target from pubkey */
    unsigned char target[20];
    suite = suite ? suite : norn_suite_sodium();
    suite->nodeid_from_pubkey(target, endpoint->pubkey);
    
    /* Sign the value */
    unsigned char sig[64];
    suite->sign(sig, value, value_len, secret);
    
    /* Create transaction */
    norn_transaction_t *txn = norn_transaction_new(&client->txn, TXN_ANNOUNCE_ENDPOINT, target);
    if (!txn) return -1;
    
    txn->user_data = user_data;
    txn->suite = suite;
    (void)callback; /* TODO: Add announce callback type */
    
    /* Store in DHT (this triggers async announce) */
    /* Sequence number should be tracked per-key (TODO) */
    uint32_t seq = 1;
    
    int ret = mainline_lookup_mutable(&client->ml, target, 1, NULL, 0,
                                       value, value_len, NULL, NULL, seq, 1,
                                       NULL, NULL, 0, 0, NULL);
    
    return ret;
}

int norn_resolve_endpoint_async(norn_client_t *client,
                                const unsigned char *pubkey,
                                const norn_crypto_suite_t *suite,
                                void *callback,
                                void *user_data) {
    if (!client || !pubkey) return -1;
    if (!client->initialized) return -1;
    if (!callback) return -1;
    
    /* Compute target from pubkey */
    unsigned char target[20];
    suite = suite ? suite : norn_suite_sodium();
    suite->nodeid_from_pubkey(target, pubkey);
    
    /* Create transaction */
    norn_transaction_t *txn = norn_transaction_new(&client->txn, TXN_RESOLVE_ENDPOINT, target);
    if (!txn) return -1;
    
    txn->resolve_callback = (norn_resolve_callback_t)callback;
    txn->user_data = user_data;
    txn->suite = suite;
    
    /* Query DHT */
    int ret = mainline_lookup_mutable(&client->ml, target, 0, NULL, 0,
                                       NULL, 0, NULL, NULL, 0, 1,
                                       NULL, NULL, 0, 0, NULL);
    
    return ret;
}

/* === Stream Multiplexing (FEAT-018, stub) === */

int norn_stream_open_async(norn_session_t *session,
                           void *callback,
                           void *user_data) {
    (void)session;
    (void)callback;
    (void)user_data;
    
    /* TODO FEAT-018: Implement stream multiplexing */
    return -1;
}

/* === NAT Traversal (FEAT-017, stub) === */

int norn_hole_punch_async(norn_client_t *client,
                          const unsigned char *target_pubkey,
                          const unsigned char *rendezvous_pubkey,
                          norn_session_callback_t callback,
                          void *user_data) {
    if (!client || !target_pubkey || !rendezvous_pubkey) return -1;
    if (!callback) return -1;
    
    /* TODO FEAT-017 Phase 3: Implement hole punching
     * 
     * Algorithm:
     * 1. Send HolePunchRequest to rendezvous via DHT
     * 2. Rendezvous coordinates with target
     * 3. Both sides send simultaneous UDP probes
     * 4. NAT creates mapping, connection established
     * 
     * Wire protocol (see FEAT-017-NAT.md):
     * - HolePunchRequest (msg_type = 0x10)
     * - HolePunchResponse (msg_type = 0x11)
     */
    
    (void)callback;
    (void)user_data;
    return -1;
}

int norn_rendezvous_enable(norn_client_t *client,
                          void *callback,
                          void *user_data) {
    if (!client) return -1;
    if (!client->initialized) return -1;
    
    /* TODO FEAT-017 Phase 3: Implement rendezvous
     * 
     * When acting as rendezvous:
     * 1. Listen for HolePunchRequest messages
     * 2. Match pairs of peers wanting to connect
     * 3. Send HolePunchResponse to both with peer's external address
     * 4. Both peers send probes simultaneously
     */
    
    (void)callback;
    (void)user_data;
    return -1;
}

int norn_relay_connect_async(norn_client_t *client,
                             const unsigned char *target_pubkey,
                             const unsigned char *relay_pubkey,
                             norn_session_callback_t callback,
                             void *user_data) {
    if (!client || !target_pubkey || !relay_pubkey) return -1;
    if (!callback) return -1;
    
    /* TODO FEAT-017 Phase 4: Implement relay connection
     * 
     * Algorithm:
     * 1. Create onion-routed circuit through relay
     * 2. Each hop adds layer of encryption
     * 3. Relay forwards without seeing payload
     * 4. End-to-end encryption between initiator and target
     * 
     * Wire protocol (see FEAT-017-NAT.md):
     * - RelayCreate (msg_type = 0x20)
     * - RelayExtend (msg_type = 0x21)
     */
    
    (void)callback;
    (void)user_data;
    return -1;
}

int norn_relay_enable(norn_client_t *client,
                      void *callback,
                      void *user_data) {
    if (!client) return -1;
    if (!client->initialized) return -1;
    
    /* TODO FEAT-017 Phase 4: Implement relay service
     * 
     * When acting as relay:
     * 1. Listen for RelayCreate messages
     * 2. Create circuit ID
     * 3. Forward traffic between circuit endpoints
     * 4. Cannot see encrypted payload
     */
    
    (void)callback;
    (void)user_data;
    return -1;
}