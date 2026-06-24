/**
 * @file norn_session.c
 * @brief Async session management implementation (FEAT-016).
 *
 * Fully async, non-blocking session management with event loop integration.
 * No blocking I/O - all operations callback-based.
 */

#include "norn_internal.h"
#include "norn_session_internal.h"
#include "norn_suite.h"
#include "channel.h"
#include "streammux.h"
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

int norn_dial_async(norn_client_t *client,
                    const unsigned char *pubkey,
                    const norn_crypto_suite_t *suite,
                    norn_session_callback_t callback,
                    void *user_data) {
    if (!client || !pubkey) return -1;
    
    /* TODO Phase 2: Resolve endpoint via DHT */
    /* TODO Phase 2: NAT traversal */
    
    /* Phase 1: Stub - invoke callback immediately */
    norn_session_t *session = norn_session_new(client, suite);
    if (!session) return -1;
    
    session->is_initiator = 1;
    memcpy(session->peer_pubkey, pubkey, session->suite->pubkey_len);
    session->callback = callback;
    session->user_data = user_data;
    
    /* Generate ephemeral key */
    if (channel_gen_ephemeral(&session->channel) != 0) {
        streammux_free(session->mux);
        free(session);
        return -1;
    }
    
    /* Register with client for event processing */
    if (norn_client_add_session(client, session) != 0) {
        streammux_free(session->mux);
        free(session);
        return -1;
    }
    
    /* Stub: Immediately invoke callback */
    session->state = NORN_SESSION_ESTABLISHED;
    if (callback) {
        callback(session, NORN_SESSION_ESTABLISHED, user_data);
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

/* === Endpoint Discovery (Async, stub) === */

int norn_announce_endpoint_async(norn_client_t *client,
                                const norn_endpoint_t *endpoint,
                                const unsigned char *secret,
                                const norn_crypto_suite_t *suite,
                                void *callback,
                                void *user_data) {
    (void)client;
    (void)endpoint;
    (void)secret;
    (void)suite;
    (void)callback;
    (void)user_data;
    
    /* TODO Phase 2: Implement async DHT announce */
    return -1;
}

int norn_resolve_endpoint_async(norn_client_t *client,
                                const unsigned char *pubkey,
                                const norn_crypto_suite_t *suite,
                                void *callback,
                                void *user_data) {
    (void)client;
    (void)pubkey;
    (void)suite;
    (void)callback;
    (void)user_data;
    
    /* TODO Phase 2: Implement async DHT resolve */
    return -1;
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