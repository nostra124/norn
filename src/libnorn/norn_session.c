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

/* Send one datagram to the session's peer. An outbound dial has its own socket
 * connect()ed to the peer, so a plain send() suffices (and Linux rejects sendto
 * with an address on a connected socket). An inbound session shares the client's
 * unconnected listen socket, so it must address the peer explicitly. */
static ssize_t session_dgram_send(norn_session_t *s, const unsigned char *buf,
                                  size_t len) {
    if (!s->shared_fd) return send(s->fd, buf, len, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = s->peer_ip;
    a.sin_port = s->peer_port;
    return sendto(s->fd, buf, len, 0, (struct sockaddr *)&a, sizeof(a));
}

/* Stream data plane (FEAT-016/018): the streammux emits id-framed segments
 * through this callback; we seal each as one datagram over the channel. The
 * peer's process_packet decrypts it back into a segment and feeds its mux. */
static int session_mux_send(void *ctx, const unsigned char *seg, size_t len) {
    norn_session_t *s = ctx;
    if (s->fd < 0 || s->state != NORN_SESSION_ESTABLISHED) return 1; /* retry once ready */
    unsigned char out[2048];
    if (len + CHANNEL_OVERHEAD > sizeof(out)) return 0; /* drop oversized (bounded MTU) */
    int sl = channel_seal(&s->channel, seg, len, out, sizeof(out));
    if (sl < 0) return 0;
    ssize_t sent = session_dgram_send(s, out, (size_t)sl);
    return sent == sl ? 0 : 1;
}

/* Grow the session's stream-wrapper array by one slot if needed. */
static int session_streams_reserve(norn_session_t *s) {
    if (s->stream_count < s->stream_cap) return 0;
    int new_cap = s->stream_cap * 2;
    norn_stream_t **g = realloc(s->streams, (size_t)new_cap * sizeof(*g));
    if (!g) return -1;
    s->streams = g;
    s->stream_cap = new_cap;
    return 0;
}

/* On the first inbound segment for a peer-initiated stream id, create a wrapper
 * and notify the accept handler (the server side of a tunnel). */
static void session_maybe_accept_stream(norn_session_t *s, uint16_t sid) {
    for (int i = 0; i < s->stream_count; i++)
        if (s->streams[i] && s->streams[i]->stream_id == sid) return;
    if (session_streams_reserve(s) != 0) return;
    norn_stream_t *st = calloc(1, sizeof(*st));
    if (!st) return;
    st->stream_id = sid;
    st->session = s;
    st->closed = 0;
    s->streams[s->stream_count++] = st;
    if (s->accept_stream_cb) s->accept_stream_cb(st, s->accept_stream_ud);
}

norn_session_t *norn_session_new(norn_client_t *client,
                                  const norn_crypto_suite_t *suite) {
    if (!client) return NULL;
    
    norn_session_t *session = calloc(1, sizeof(*session));
    if (!session) return NULL;
    
    session->client = client;
    session->suite = suite ? suite : norn_suite_sodium();
    session->state = NORN_SESSION_CONNECTING;
    session->fd = -1;
    session->next_stream_id = 1;  /* Initiator uses odd IDs */
    
    session->mux = streammux_new(session_mux_send, session);
    if (!session->mux) {
        free(session);
        return NULL;
    }
    
    /* Initialize stream tracking */
    session->stream_cap = 16;
    session->streams = calloc(session->stream_cap, sizeof(*session->streams));
    if (!session->streams) {
        streammux_free(session->mux);
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
    unsigned char ephemeral_pub[32];  /* FEAT-023: Ephemeral key for hole punch */
    unsigned char ephemeral_sec[32];  /* FEAT-023: Ephemeral secret */
    uint8_t rendezvous_pubkey[32];    /* FEAT-023: Rendezvous peer pubkey */
} dial_context_t;

/**
 * Callback for endpoint resolution.
 */

/**
 * FEAT-023: Create session from probe detection.
 * Called from norn_impl.c when a probe is received.
 */
int norn_session_from_probe(norn_client_t *client,
                             void *dial_ctx_ptr,
                             uint32_t from_ip,
                             uint16_t from_port) {
    dial_context_t *ctx = (dial_context_t *)dial_ctx_ptr;
    if (!ctx || !ctx->callback) return -1;
    
    /* Create session */
    norn_session_t *session = norn_session_new(client, ctx->suite);
    if (!session) {
        ctx->state = DIAL_FAILED;
        ctx->callback(NULL, NORN_SESSION_CLOSED, ctx->user_data);
        free(ctx);
        return -1;
    }
    
    session->is_initiator = 1;
    session->peer_ip = from_ip;
    session->peer_port = from_port;
    memcpy(session->peer_pubkey, ctx->peer_pubkey, session->suite->pubkey_len);
    
    /* Set ephemeral keys from hole punch */
    memcpy(session->channel.eph_pub, ctx->ephemeral_pub, 32);
    memcpy(session->channel.eph_sec, ctx->ephemeral_sec, 32);
    
    /* Mark session as established (simplified - no full handshake) */
    session->state = NORN_SESSION_ESTABLISHED;
    session->channel.established = 1;
    
    /* Notify callback */
    ctx->callback(session, NORN_SESSION_ESTABLISHED, ctx->user_data);
    free(ctx);
    
    return 0;
}

/**
 * FEAT-023: Hole punch response callback.
 */
static void on_holepunch_response(norn_client_t *client,
                                   const norn_holepunch_resp_t *resp,
                                   void *user_data) {
    dial_context_t *ctx = (dial_context_t *)user_data;
    if (!ctx || !resp) {
        if (ctx) {
            ctx->state = DIAL_FAILED;
            if (ctx->callback) {
                ctx->callback(NULL, NORN_SESSION_CLOSED, ctx->user_data);
            }
            free(ctx);
        }
        return;
    }
    
    /* FEAT-023: Send simultaneous probes */
    if (resp->peer_external_ip != 0 && resp->peer_external_port != 0) {
        ctx->state = DIAL_HOLEPUNCH;
        
        /* Store peer's ephemeral pubkey for later session creation */
        memcpy(ctx->ephemeral_pub, resp->peer_ephemeral_pubkey, 32);
        
        /* Send probes to peer */
        norn_send_probes(client, ctx->ephemeral_pub, 
                         resp->peer_external_ip, resp->peer_external_port, 3, 100);
        
        /* Don't free ctx - it will be used when probe is detected */
        return;
    }
    
    ctx->state = DIAL_FAILED;
    if (ctx->callback) {
        ctx->callback(NULL, NORN_SESSION_CLOSED, ctx->user_data);
    }
    free(ctx);
}

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
        
        /* FEAT-023: Generate ephemeral key for this session */
        crypto_box_keypair(ctx->ephemeral_pub, ctx->ephemeral_sec);
        
        /* Store rendezvous pubkey (endpoint itself is the rendezvous) */
        memcpy(ctx->rendezvous_pubkey, endpoint->pubkey, 32);
        
        /* Send hole punch request */
        int ret = norn_send_holepunch_req_async(ctx->client,
                                                 ctx->peer_pubkey,
                                                 ctx->rendezvous_pubkey,
                                                 ctx->ephemeral_pub,
                                                 on_holepunch_response,
                                                 ctx);
        if (ret == 0) {
            /* Request sent successfully - waiting for response */
            return;
        }
        
        /* Hole punch failed - fall through to relay */
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

    norn_session_t *session = norn_session_new(client, suite);
    if (!session) return -1;

    session->is_initiator = 1;
    /* Identity is the client's own keypair, so the peer authenticates us as the
     * node it expects (e.g. a cluster member) — not a throwaway key. */
    norn_session_set_identity(session, client->self_pub, client->self_sec);
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

    /* Verify packet is from expected peer */
    if (session->peer_ip != 0 && session->peer_ip != from_ip) return -1;
    if (session->peer_port != 0 && session->peer_port != from_port) return -1;

    /* Established: a datagram is a sealed stream-mux segment. Decrypt it,
     * surface any newly peer-initiated stream, and feed the mux. */
    if (session->state == NORN_SESSION_ESTABLISHED) {
        unsigned char pt[2048];
        int pl = channel_open(&session->channel, data, len, pt, sizeof(pt));
        if (pl < STREAMMUX_FRAME) return -1;
        uint16_t sid = (uint16_t)(((uint16_t)pt[0] << 8) | pt[1]);
        session_maybe_accept_stream(session, sid);
        streammux_input(session->mux, pt, (size_t)pl, 0);
        return 0;
    }

    if (session->state != NORN_SESSION_CONNECTING) return -1;
    
    /* Process based on handshake state */
    if (session->is_initiator) {
        /* Initiator expects RESP */
        if (session->hs_state != HS_INIT_SENT) return -1;
        
        unsigned char confirm_msg[CHANNEL_CONFIRM_LEN];
        int confirm_len = norn_session_confirm_resp(session, data, len,
                                                     confirm_msg, sizeof(confirm_msg));
        if (confirm_len < 0) return -1;
        
        /* Send CONFIRM */
        ssize_t sent = session_dgram_send(session, confirm_msg, (size_t)confirm_len);
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
            ssize_t sent = session_dgram_send(session, resp_msg, (size_t)resp_len);
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
        /* Build and send INIT. The initiator's socket is connect()ed to the
         * peer, so this goes via send() — sendto() with an address on a
         * connected UDP socket is rejected with EISCONN on macOS/BSD. */
        unsigned char init_msg[CHANNEL_INIT_LEN];
        int init_len = norn_session_build_init(session, init_msg, sizeof(init_msg));
        if (init_len < 0) return -1;

        ssize_t sent = session_dgram_send(session, init_msg, (size_t)init_len);
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

/* Route a datagram that arrived on the shared listen socket. If it belongs to an
 * existing inbound session (matched by peer endpoint), feed it there; otherwise
 * treat it as a new peer's INIT: spin up a responder session bound to the shared
 * listen fd, run the handshake, and hand it to the accept callback. */
static void accept_or_route(norn_client_t *client, const unsigned char *buf,
                            size_t len, uint32_t ip, uint16_t port) {
    for (int i = 0; i < client->session_count; i++) {
        norn_session_t *s = client->sessions[i];
        if (s->shared_fd && s->state != NORN_SESSION_CLOSED &&
            s->peer_ip == ip && s->peer_port == port) {
            norn_session_process_packet(s, buf, len, ip, port);
            return;
        }
    }

    if (!client->listen_callback) return; /* not listening for new peers */

    norn_session_t *s = norn_session_new(client, client->listen_suite);
    if (!s) return;
    s->fd = client->listen_fd;
    s->shared_fd = 1; /* shared socket: never closed/recv-drained by the session */
    s->is_initiator = 0;
    s->state = NORN_SESSION_CONNECTING;
    s->hs_state = HS_NONE;
    s->peer_ip = ip;
    s->peer_port = port;
    norn_session_set_identity(s, client->self_pub, client->self_sec);
    if (channel_gen_ephemeral(&s->channel) != 0 ||
        norn_client_add_session(client, s) != 0) {
        norn_session_free(s);
        return;
    }
    /* Consume the INIT we already read (sends RESP back via the shared fd). */
    norn_session_process_packet(s, buf, len, ip, port);
    client->listen_callback(s, client->listen_user_data);
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

        /* Drive stream-mux timers (retransmit/flush) on established sessions. */
        if (session->state == NORN_SESSION_ESTABLISHED) {
            streammux_tick(session->mux, now_ms);
        }

        /* Process incoming packets if fd is set. Drain all queued datagrams so a
         * burst of stream segments is delivered in one tick. Inbound sessions
         * share the listen socket and are fed by the listener block below. */
        if (session->fd >= 0 && !session->shared_fd) {
            unsigned char buf[4096];
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);

            ssize_t len;
            while ((len = recvfrom(session->fd, buf, sizeof(buf), MSG_DONTWAIT,
                                   (struct sockaddr *)&from, &fromlen)) > 0) {
                norn_session_process_packet(session, buf, len,
                                             from.sin_addr.s_addr, from.sin_port);
                processed++;
            }
        }
    }

    /* Process listener socket: demux datagrams to inbound sessions / accept new
     * peers. Drain all queued datagrams so a burst is handled in one tick. */
    if (client->listen_fd >= 0) {
        unsigned char buf[4096];
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);

        ssize_t len;
        while ((len = recvfrom(client->listen_fd, buf, sizeof(buf), MSG_DONTWAIT,
                               (struct sockaddr *)&from, &fromlen)) > 0) {
            accept_or_route(client, buf, (size_t)len, from.sin_addr.s_addr,
                            from.sin_port);
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
    
    /* Add session sockets (inbound sessions share the listen fd, already added) */
    for (int i = 0; i < client->session_count && count < max_fds; i++) {
        norn_session_t *session = client->sessions[i];

        if (session->fd >= 0 && !session->shared_fd &&
            session->state != NORN_SESSION_CLOSED) {
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

    /* Don't close a shared listen fd — it's owned by the client, not us. */
    if (session->fd >= 0 && !session->shared_fd) {
        close(session->fd);
    }
    
    /* Free all streams */
    if (session->streams) {
        for (int i = 0; i < session->stream_count; i++) {
            if (session->streams[i]) {
                free(session->streams[i]);
            }
        }
        free(session->streams);
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

norn_stream_t *norn_stream_open_async(norn_session_t *session,
                                      norn_stream_callback_t callback,
                                      void *user_data) {
    if (!session || !callback) return NULL;
    if (session->state != NORN_SESSION_ESTABLISHED) return NULL;
    
    /* Allocate stream */
    norn_stream_t *stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    
    /* Assign stream ID (odd for initiator, even for responder) */
    stream->stream_id = session->next_stream_id;
    stream->session = session;
    stream->callback = callback;
    stream->user_data = user_data;
    stream->closed = 0;
    
    session->next_stream_id += 2;  /* Odd/even pattern */
    
    /* Open stream in mux */
    if (streammux_open(session->mux, stream->stream_id) != 0) {
        free(stream);
        return NULL;
    }
    
    /* Add to session's stream list */
    if (session->stream_count >= session->stream_cap) {
        /* Grow array */
        int new_cap = session->stream_cap * 2;
        norn_stream_t **new_streams = realloc(session->streams,
                                               new_cap * sizeof(*new_streams));
        if (!new_streams) {
            free(stream);
            return NULL;
        }
        session->streams = new_streams;
        session->stream_cap = new_cap;
    }
    
    session->streams[session->stream_count++] = stream;
    
    /* Notify callback */
    callback(stream, NORN_STREAM_READY, user_data);

    return stream;
}

int norn_session_set_accept_stream(norn_session_t *session,
                                   void (*cb)(norn_stream_t *stream, void *user_data),
                                   void *user_data) {
    if (!session) return -1;
    session->accept_stream_cb = cb;
    session->accept_stream_ud = user_data;
    return 0;
}

int norn_stream_write(norn_stream_t *stream,
                      const unsigned char *data,
                      size_t len) {
    if (!stream || !data || len == 0) return -1;
    if (stream->closed) return -1;
    
    uint32_t now_ms = 0;  /* TODO: Get actual time */
    
    return streammux_write(stream->session->mux, stream->stream_id,
                          data, len, now_ms);
}

int norn_stream_read(norn_stream_t *stream,
                     unsigned char *buf,
                     size_t cap) {
    if (!stream || !buf || cap == 0) return -1;
    
    return streammux_read(stream->session->mux, stream->stream_id,
                         buf, cap);
}

size_t norn_stream_readable(const norn_stream_t *stream) {
    if (!stream) return 0;
    
    return streammux_readable(stream->session->mux, stream->stream_id);
}

int norn_stream_close(norn_stream_t *stream) {
    if (!stream) return -1;
    if (stream->closed) return -1;
    
    uint32_t now_ms = 0;  /* TODO: Get actual time */
    
    streammux_finish(stream->session->mux, stream->stream_id, now_ms);
    stream->closed = 1;
    
    /* Notify callback */
    if (stream->callback) {
        stream->callback(stream, NORN_STREAM_CLOSED, stream->user_data);
    }
    
    return 0;
}

int norn_stream_reset(norn_stream_t *stream) {
    if (!stream) return -1;
    if (stream->closed) return -1;
    
    stream->closed = 1;
    
    /* Notify callback */
    if (stream->callback) {
        stream->callback(stream, NORN_STREAM_RESET, stream->user_data);
    }
    
    return 0;
}

int norn_stream_peer_closed(const norn_stream_t *stream) {
    if (!stream) return 0;
    
    return streammux_peer_finished(stream->session->mux, stream->stream_id);
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