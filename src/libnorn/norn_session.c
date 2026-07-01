/* SPDX-License-Identifier: MIT */
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
#include "norn_upnp.h"
#include "bep44.h"
#include "crypto.h"
#include "net.h"
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
 * unconnected listen socket, so it must address the peer explicitly. Relay
 * sessions (relay_enabled) wrap the payload in RelayForward and send via the
 * shared DHT socket to the relay node. */
static ssize_t session_dgram_send(norn_session_t *s, const unsigned char *buf,
                                  size_t len) {
    if (s->relay_enabled) {
        if (len > NORN_RELAY_MAX_PAYLOAD) return -1;
        norn_relay_forward_t fwd;
        fwd.msg_type = NORN_MSG_RELAY_FORWARD;
        memcpy(fwd.session_id, s->relay_session_id, NORN_RELAY_SESSION_ID_LEN);
        fwd.payload_len = (uint16_t)len;
        memcpy(fwd.payload, buf, len);
        uint8_t out[1 + NORN_RELAY_SESSION_ID_LEN + 2 + NORN_RELAY_MAX_PAYLOAD];
        size_t out_len = 0;
        if (norn_encode_relay_forward(&fwd, out, &out_len) != 0) return -1;
        return net_send(&s->client->net, out, out_len, s->relay_ip, s->relay_port);
    }
    if (!s->shared_fd) return send(s->fd, buf, len, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = s->peer_ip;
    a.sin_port = s->peer_port;
    return sendto(s->fd, buf, len, 0, (struct sockaddr *)&a, sizeof(a));
}

/* Application-frame header on the post-decrypt plaintext (FEAT-033): one byte of
 * kind + a 2-byte service id, so reliable streams and unreliable datagrams of
 * many app protocols share one session/port without collision. */
#define APPFRAME_HDR   3
#define APPKIND_STREAM 0
#define APPKIND_DGRAM  1

/* Seal `len` bytes of plaintext (already app-framed) and send as one datagram. */
static int session_seal_send(norn_session_t *s, const unsigned char *frame,
                             size_t len) {
    unsigned char out[2048];
    if (len + CHANNEL_OVERHEAD > sizeof(out)) return -1;
    int sl = channel_seal(&s->channel, frame, len, out, sizeof(out));
    if (sl < 0) return -1;
    ssize_t sent = session_dgram_send(s, out, (size_t)sl);
    return sent == sl ? 0 : -1;
}

/* Stream data plane (FEAT-016/018/033): a per-service streammux emits id-framed
 * segments through this callback; we prepend [STREAM][service] and seal each as
 * one datagram. The peer's process_packet decrypts it, reads the service, and
 * feeds the matching per-service mux. ctx is the owning svc_mux entry. */
static int session_mux_send(void *ctx, const unsigned char *seg, size_t len) {
    struct svc_mux *sm = ctx;
    norn_session_t *s = sm->session;
    if (s->fd < 0 || s->state != NORN_SESSION_ESTABLISHED) return 1; /* retry once ready */
    unsigned char frame[2048];
    if (APPFRAME_HDR + len > sizeof(frame)) return 0; /* drop oversized (bounded MTU) */
    frame[0] = APPKIND_STREAM;
    frame[1] = (unsigned char)(sm->service >> 8);
    frame[2] = (unsigned char)(sm->service & 0xff);
    memcpy(frame + APPFRAME_HDR, seg, len);
    return session_seal_send(s, frame, APPFRAME_HDR + len) == 0 ? 0 : 1;
}

/* Find (or, if `create`, lazily allocate) the per-service stream mux. Streams of
 * different services live in separate muxes — separate id spaces and flow
 * control — so they never collide. Returns NULL if the table is full or alloc
 * fails. The odd/even stream-id base keeps the two ends from picking the same
 * id within a service. */
static struct svc_mux *session_get_svc(norn_session_t *s, norn_service_t service,
                                       int create) {
    int slot = -1;
    for (int i = 0; i < NORN_MAX_SERVICES; i++) {
        if (s->svc[i].active && s->svc[i].service == service) return &s->svc[i];
        if (!s->svc[i].active && slot < 0) slot = i;
    }
    if (!create || slot < 0) return NULL;
    struct svc_mux *sm = &s->svc[slot];
    sm->session = s;
    sm->service = service;
    sm->next_stream_id = s->is_initiator ? 1 : 2; /* odd initiator / even responder */
    sm->mux = streammux_new(session_mux_send, sm);
    if (!sm->mux) return NULL;
    sm->active = 1;
    return sm;
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

/* On the first inbound segment for a peer-initiated (service, stream id), create
 * a wrapper and notify the right accept handler: service 0 keeps the legacy
 * per-session callback (tunnels); other services dispatch via the client's
 * service registry (cluster Raft, served-KV, consumer apps). */
static void session_maybe_accept_stream(norn_session_t *s, norn_service_t service,
                                        struct svc_mux *sm, uint16_t sid) {
    for (int i = 0; i < s->stream_count; i++)
        if (s->streams[i] && s->streams[i]->service == service &&
            s->streams[i]->stream_id == sid)
            return;
    if (session_streams_reserve(s) != 0) return;
    norn_stream_t *st = calloc(1, sizeof(*st));
    if (!st) return;
    st->stream_id = sid;
    st->service = service;
    st->mux = sm->mux;
    st->session = s;
    st->closed = 0;
    s->streams[s->stream_count++] = st;
    if (service == NORN_SVC_DEFAULT && s->accept_stream_cb) {
        s->accept_stream_cb(st, s->accept_stream_ud);
        return;
    }
    void (*cb)(norn_stream_t *, void *) = NULL;
    void *ud = NULL;
    if (norn_client_stream_svc(s->client, service, &cb, &ud) == 0 && cb)
        cb(st, ud);
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
    /* Per-service muxes (session->svc[]) are created lazily on first stream of a
     * service — see session_get_svc. */

    /* Initialize stream tracking */
    session->stream_cap = 16;
    session->streams = calloc(session->stream_cap, sizeof(*session->streams));
    if (!session->streams) {
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

void norn_session_set_signer(norn_session_t *session, channel_signer_fn fn,
                             void *ud) {
    if (!session) return;
    session->signer = fn;
    session->signer_ud = ud;
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
    uint32_t resolve_txn_id;
    unsigned char ephemeral_pub[32];
    unsigned char ephemeral_sec[32];
    uint8_t rendezvous_pubkey[32];
    uint32_t rendezvous_ip;
    uint16_t rendezvous_port;
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

    /* Probe confirmed the path is open — run the full channel handshake
     * over it so the session gets a real derived key, not a fake one. */
    norn_direct_endpoint_t ep = { .ip = from_ip, .port = from_port };
    int ret = norn_dial_direct_async(client, &ep, ctx->peer_pubkey, ctx->suite,
                                     ctx->callback, ctx->user_data);
    free(ctx);
    return ret;
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
        
        memcpy(ctx->rendezvous_pubkey, endpoint->pubkey, 32);
        ctx->rendezvous_ip = endpoint->ip;
        ctx->rendezvous_port = endpoint->port;

        int ret = norn_send_holepunch_req_async(ctx->client,
                                                 ctx->peer_pubkey,
                                                 ctx->rendezvous_ip,
                                                 ctx->rendezvous_port,
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
    if (!client || !endpoint) return -1;
    /* pubkey may be NULL: an "unauthenticated" dial that learns the peer's
     * pubkey from the handshake (channel_hs_confirm extracts it). The caller
     * is then responsible for verifying the learned pubkey (e.g. that its
     * BEP-44 target matches a known DHT node id). */

    norn_session_t *session = norn_session_new(client, suite);
    if (!session) return -1;

    session->is_initiator = 1;
    /* Identity is the client's own keypair, so the peer authenticates us as the
     * node it expects (e.g. a cluster member) — not a throwaway key. */
    norn_session_set_identity(session, client->self_pub, client->self_sec);
    norn_session_set_signer(session, client->signer, client->signer_ud);
    if (pubkey)
        memcpy(session->peer_pubkey, pubkey, session->suite->pubkey_len);
    
    session->peer_ip = endpoint->ip;
    session->peer_port = endpoint->port;
    
    if (channel_gen_ephemeral(&session->channel) != 0) {
        free(session->streams);
        free(session);
        return -1;
    }

    /* Create UDP socket */
    session->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (session->fd < 0) {
        free(session->streams);
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
            free(session->streams);
            free(session);
            return -1;
        }
    }

    /* Register with client */
    if (norn_client_add_session(client, session) != 0) {
        close(session->fd);
        free(session->streams);
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
    
    /* Reuse the DHT socket (client->net.fd) for session traffic too.
     * DHT (bencoded) and norn (binary) packets are distinguished by
     * dispatch_response in norn_tick. Avoids a second UDP socket on the
     * same port (which Linux delivers to only one of anyway). */
    if (client->net.fd < 0) return -1;
    
    /* Store listener callback + suite on the shared DHT socket */
    client->listen_fd = client->net.fd;
    client->listen_callback = callback;
    client->listen_user_data = user_data;
    client->listen_suite = suite ? suite : norn_suite_sodium();
    
    /* Bootstrap to the well-known Mainline DHT routers (router.bittorrent.com,
     * router.utorrent.com, dht.transmissionbt.com) so the node joins the global
     * DHT network immediately. These hosts are hard-coded in mainline_init() and
     * are always available unless private_mode is set in norn_config_t. The
     * bootstrap is async — norn_tick() in the event loop drives it. */
    norn_bootstrap(client);

    /* Announce this node via Bonjour/mDNS and discover other local norn nodes,
     * so that nodes on the same LAN find each other automatically. Best-effort:
     * if Avahi is not running, this silently fails. */
    if (!client->bonjour) {
        uint16_t dht_port = net_get_bound_port(&client->net);
        if (dht_port == 0) dht_port = ntohs(port);
        client->bonjour = norn_bonjour_new(client, dht_port, client->self_pub);
    }

    /* NAT traversal: try NAT-PMP first, then UPnP, to map the UDP port on
     * the router so the node is reachable from the public Internet. When the
     * mapping succeeds the authoritative public endpoint is fed back into
     * the DHT layer so the node can announce itself correctly. Best-effort:
     * if neither NAT-PMP nor UPnP work the node operates behind NAT and
     * relies on relay / hole-punching. */
    {
        norn_upnp_result_t nat;
        memset(&nat, 0, sizeof(nat));
        uint16_t actual_port = net_get_bound_port(&client->net);
        if (actual_port == 0) actual_port = ntohs(port);
        if (norn_auto_port_mapping(actual_port, "UDP", &nat) == 0 && nat.success) {
            uint16_t local_port = actual_port;
            net_set_mapped_endpoint(&client->net, nat.external_ip,
                                     nat.external_port);
            fprintf(stderr, "norn: NAT-PMP/UPnP mapped port %u -> %s:%u\n",
                    (unsigned)local_port,
                    inet_ntoa(*(struct in_addr*)&nat.external_ip),
                    (unsigned)ntohs(nat.external_port));
        }
    }

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

    /* Established: a datagram is a sealed app frame. Decrypt it, read the
     * [kind][service] header, and route to the matching per-service mux (reliable
     * stream) or the registered datagram handler (unreliable). */
    if (session->state == NORN_SESSION_ESTABLISHED) {
        unsigned char pt[2048];
        int pl = channel_open(&session->channel, data, len, pt, sizeof(pt));
        if (pl < APPFRAME_HDR) return -1;
        int kind = pt[0];
        norn_service_t service = (norn_service_t)(((uint16_t)pt[1] << 8) | pt[2]);
        const unsigned char *body = pt + APPFRAME_HDR;
        size_t blen = (size_t)pl - APPFRAME_HDR;
        if (kind == APPKIND_STREAM) {
            if (blen < STREAMMUX_FRAME) return -1;
            struct svc_mux *sm = session_get_svc(session, service, 1);
            if (!sm) return -1;
            uint16_t sid = (uint16_t)(((uint16_t)body[0] << 8) | body[1]);
            session_maybe_accept_stream(session, service, sm, sid);
            streammux_input(sm->mux, body, blen, 0);
            return 0;
        }
        if (kind == APPKIND_DGRAM) {
            norn_datagram_cb_t cb = NULL;
            void *ud = NULL;
            if (norn_client_dgram_svc(session->client, service, &cb, &ud) == 0 && cb)
                cb(session, body, blen, ud);
            return 0;
        }
        return -1; /* unknown app-frame kind */
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
static int accept_or_route(norn_client_t *client, const unsigned char *buf,
                            size_t len, uint32_t ip, uint16_t port) {
    for (int i = 0; i < client->session_count; i++) {
        norn_session_t *s = client->sessions[i];
        if (s->shared_fd && s->state != NORN_SESSION_CLOSED &&
            s->peer_ip == ip && s->peer_port == port) {
            norn_session_process_packet(s, buf, len, ip, port);
            return 0;
        }
    }

    if (!client->listen_callback) return -1; /* not listening for new peers */

    /* Only treat as a new INIT if the packet starts with CHANNEL_MAGIC ("DHCH").
     * This avoids creating zombie sessions for DHT/binary-protocol packets that
     * happen to arrive from an address that has no session yet. */
    if (len < 4 || memcmp(buf, "DHCH", 4) != 0) return -1;

    norn_session_t *s = norn_session_new(client, client->listen_suite);
    if (!s) return -1;
    s->fd = client->listen_fd;
    s->shared_fd = 1; /* shared socket: never closed/recv-drained by the session */
    s->is_initiator = 0;
    s->state = NORN_SESSION_CONNECTING;
    s->hs_state = HS_NONE;
    s->peer_ip = ip;
    s->peer_port = port;
    norn_session_set_identity(s, client->self_pub, client->self_sec);
    norn_session_set_signer(s, client->signer, client->signer_ud);
    if (channel_gen_ephemeral(&s->channel) != 0 ||
        norn_client_add_session(client, s) != 0) {
        norn_session_free(s);
        return -1;
    }
    /* Consume the INIT we already read (sends RESP back via the shared fd). */
    norn_session_process_packet(s, buf, len, ip, port);
    client->listen_callback(s, client->listen_user_data);
    return 0;
}

int norn_session_dispatch_udp(norn_client_t *client, const unsigned char *buf,
                                size_t len, uint32_t ip, uint16_t port) {
    if (!client || !buf || len == 0) return -1;
    return accept_or_route(client, buf, len, ip, port);
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

        /* Drive every active per-service stream-mux's timers on established
         * sessions (retransmit/flush). */
        if (session->state == NORN_SESSION_ESTABLISHED) {
            for (int si = 0; si < NORN_MAX_SERVICES; si++)
                if (session->svc[si].active)
                    streammux_tick(session->svc[si].mux, now_ms);
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

    /* Session packets now arrive via norn_tick → dispatch_response (which
     * calls norn_session_dispatch_udp). No separate listen_fd recvfrom here. */

    return processed;
}

int norn_get_session_fds(norn_client_t *client,
                         int *fds,
                         int *events,
                         int max_fds) {
    if (!client || !fds || !events) return -1;
    
    int count = 0;
    
    /* listen_fd is now the same as net.fd (unified socket); norn_tick already
     * polls net.fd, so we don't add it here to avoid duplicate poll entries. */
    
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

    /* Free every per-service stream mux. */
    for (int i = 0; i < NORN_MAX_SERVICES; i++) {
        if (session->svc[i].active) {
            streammux_free(session->svc[i].mux);
        }
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
    int len = session->signer
        ? channel_hs_accept_signed(&session->channel, session->self_pubkey,
                                   session->signer, session->signer_ud,
                                   init_msg, init_len, peer_pubkey, out, outcap)
        : channel_hs_accept(&session->channel, session->self_pubkey,
                            session->self_secret, init_msg, init_len,
                            peer_pubkey, out, outcap);

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
    int len = session->signer
        ? channel_hs_confirm_signed(&session->channel, session->self_pubkey,
                                    session->signer, session->signer_ud,
                                    resp_msg, resp_len, peer_pubkey, out, outcap)
        : channel_hs_confirm(&session->channel, session->self_pubkey,
                             session->self_secret, resp_msg, resp_len,
                             peer_pubkey, out, outcap);

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

norn_stream_t *norn_stream_open_svc(norn_session_t *session,
                                    norn_service_t service,
                                    norn_stream_callback_t callback,
                                    void *user_data) {
    if (!session || !callback) return NULL;
    if (session->state != NORN_SESSION_ESTABLISHED) return NULL;

    struct svc_mux *sm = session_get_svc(session, service, 1);
    if (!sm) return NULL;

    /* Allocate stream */
    norn_stream_t *stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;

    /* Assign stream ID (odd for initiator, even for responder), per service */
    stream->stream_id = sm->next_stream_id;
    stream->service = service;
    stream->mux = sm->mux;
    stream->session = session;
    stream->callback = callback;
    stream->user_data = user_data;
    stream->closed = 0;

    sm->next_stream_id += 2;  /* Odd/even pattern */

    /* Open stream in the service's mux */
    if (streammux_open(sm->mux, stream->stream_id) != 0) {
        free(stream);
        return NULL;
    }

    /* Add to session's stream list */
    if (session_streams_reserve(session) != 0) {
        free(stream);
        return NULL;
    }

    session->streams[session->stream_count++] = stream;

    /* Notify callback */
    callback(stream, NORN_STREAM_READY, user_data);

    return stream;
}

norn_stream_t *norn_stream_open_async(norn_session_t *session,
                                      norn_stream_callback_t callback,
                                      void *user_data) {
    return norn_stream_open_svc(session, NORN_SVC_DEFAULT, callback, user_data);
}

norn_session_t *norn_stream_session(const norn_stream_t *stream) {
    return stream ? stream->session : NULL;
}

/* Send one unreliable datagram tagged with a service (FEAT-033). */
int norn_session_send_datagram(norn_session_t *session, norn_service_t service,
                               const unsigned char *data, size_t len) {
    if (!session || !data) return -1;
    if (session->state != NORN_SESSION_ESTABLISHED) return -1;
    unsigned char frame[2048];
    if (APPFRAME_HDR + len > sizeof(frame)) return -1;
    frame[0] = APPKIND_DGRAM;
    frame[1] = (unsigned char)(service >> 8);
    frame[2] = (unsigned char)(service & 0xff);
    memcpy(frame + APPFRAME_HDR, data, len);
    return session_seal_send(session, frame, APPFRAME_HDR + len);
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
    
    return streammux_write(stream->mux, stream->stream_id,
                          data, len, now_ms);
}

int norn_stream_read(norn_stream_t *stream,
                     unsigned char *buf,
                     size_t cap) {
    if (!stream || !buf || cap == 0) return -1;
    
    return streammux_read(stream->mux, stream->stream_id,
                         buf, cap);
}

size_t norn_stream_readable(const norn_stream_t *stream) {
    if (!stream) return 0;
    
    return streammux_readable(stream->mux, stream->stream_id);
}

int norn_stream_close(norn_stream_t *stream) {
    if (!stream) return -1;
    if (stream->closed) return -1;
    
    uint32_t now_ms = 0;  /* TODO: Get actual time */
    
    streammux_finish(stream->mux, stream->stream_id, now_ms);
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
    
    return streammux_peer_finished(stream->mux, stream->stream_id);
}

/* === NAT Traversal (FEAT-023) === */

/* Resolution callback used by norn_hole_punch_async when the rendezvous
 * endpoint isn't already in the cache. */
static void on_rendezvous_resolved(const norn_endpoint_t *ep, void *user_data) {
    dial_context_t *ctx = (dial_context_t *)user_data;
    if (!ep || ep->ip == 0) {
        ctx->state = DIAL_FAILED;
        ctx->callback(NULL, NORN_SESSION_CLOSED, ctx->user_data);
        free(ctx);
        return;
    }
    int ret = norn_send_holepunch_req_async(ctx->client,
                                             ctx->peer_pubkey,
                                             ep->ip, ep->port,
                                             ctx->ephemeral_pub,
                                             on_holepunch_response,
                                             ctx);
    if (ret != 0) {
        ctx->state = DIAL_FAILED;
        ctx->callback(NULL, NORN_SESSION_CLOSED, ctx->user_data);
        free(ctx);
    }
}

int norn_hole_punch_async(norn_client_t *client,
                          const unsigned char *target_pubkey,
                          const unsigned char *rendezvous_pubkey,
                          norn_session_callback_t callback,
                          void *user_data) {
    if (!client || !target_pubkey || !rendezvous_pubkey || !callback) return -1;

    dial_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return -1;

    ctx->client = client;
    ctx->suite = norn_suite_sodium();
    ctx->callback = callback;
    ctx->user_data = user_data;
    ctx->state = DIAL_HOLEPUNCH;
    memcpy(ctx->peer_pubkey, target_pubkey, ctx->suite->pubkey_len);
    memcpy(ctx->rendezvous_pubkey, rendezvous_pubkey, 32);
    crypto_box_keypair(ctx->ephemeral_pub, ctx->ephemeral_sec);

    const norn_endpoint_t *cached =
        norn_endpoint_cache_lookup(&client->endpoint_cache, rendezvous_pubkey);
    if (cached && cached->ip != 0) {
        int ret = norn_send_holepunch_req_async(client, target_pubkey,
                                                 cached->ip, cached->port,
                                                 ctx->ephemeral_pub,
                                                 on_holepunch_response, ctx);
        if (ret != 0) { free(ctx); return -1; }
        return 0;
    }

    int ret = norn_resolve_endpoint_async(client, rendezvous_pubkey, ctx->suite,
                                           on_rendezvous_resolved, ctx);
    if (ret != 0) { free(ctx); return -1; }
    return 0;
}

int norn_rendezvous_enable(norn_client_t *client,
                          void *callback,
                          void *user_data) {
    if (!client || !client->initialized) return -1;
    client->rendezvous_enabled = 1;
    client->rendezvous_callback = callback;
    client->rendezvous_user_data = user_data;
    return 0;
}

int norn_relay_connect_async(norn_client_t *client,
                             const unsigned char *target_pubkey,
                             const unsigned char *relay_pubkey,
                             norn_session_callback_t callback,
                             void *user_data) {
    if (!client || !target_pubkey || !relay_pubkey || !callback) return -1;
    if (!client->initialized) return -1;

    const norn_endpoint_t *relay_ep = norn_endpoint_cache_lookup(
        &client->endpoint_cache, relay_pubkey);
    if (!relay_ep || relay_ep->ip == 0) return -1;

    int slot = -1;
    for (int i = 0; i < 8; i++) {
        if (!client->relay_pending[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    uint8_t session_id[NORN_RELAY_SESSION_ID_LEN];
    randombytes_buf(session_id, sizeof(session_id));

    norn_relay_create_t req;
    req.msg_type = NORN_MSG_RELAY_CREATE;
    memcpy(req.target_pubkey, target_pubkey, 32);
    memcpy(req.session_id, session_id, NORN_RELAY_SESSION_ID_LEN);
    bf_sign(req.signature, (const unsigned char *)&req,
            offsetof(norn_relay_create_t, signature), client->self_sec);

    uint8_t buf[NORN_RELAY_CREATE_LEN];
    if (norn_encode_relay_create(&req, buf) != 0) return -1;
    if (net_send(&client->net, buf, sizeof(buf), relay_ep->ip, relay_ep->port) < 0) return -1;

    memcpy(client->relay_pending[slot].session_id, session_id, NORN_RELAY_SESSION_ID_LEN);
    memcpy(client->relay_pending[slot].relay_pubkey, relay_pubkey, 32);
    client->relay_pending[slot].relay_ip = relay_ep->ip;
    client->relay_pending[slot].relay_port = relay_ep->port;
    memcpy(client->relay_pending[slot].peer_pubkey, target_pubkey, 32);
    client->relay_pending[slot].callback = callback;
    client->relay_pending[slot].user_data = user_data;
    client->relay_pending[slot].suite = norn_suite_sodium();
    client->relay_pending[slot].active = 1;
    return 0;
}

int norn_relay_enable(norn_client_t *client,
                      void *callback,
                      void *user_data) {
    if (!client || !client->initialized) return -1;
    client->relay.enabled = 1;
    client->rendezvous_callback = callback;
    client->rendezvous_user_data = user_data;
    return 0;
}