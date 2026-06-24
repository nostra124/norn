/**
 * @file norn_session_udp.c
 * @brief UDP transport for direct session handshake (FEAT-016 Phase 1).
 *
 * Provides blocking UDP send/recv for handshake messages.
 * This is a simplified implementation for testing; production use
 * should integrate with event loops (libuv, libevent, etc.).
 */

#include "norn_session_internal.h"
#include "channel.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* Timeout for handshake operations (milliseconds) */
#define HANDSHAKE_TIMEOUT_MS 5000

/**
 * @brief Send a handshake message via UDP.
 *
 * @param fd UDP socket
 * @param msg Message buffer
 * @param len Message length
 * @param ip Destination IP (network byte order)
 * @param port Destination port (network byte order)
 * @return 0 on success, -1 on error
 */
static int udp_send(int fd, const unsigned char *msg, size_t len,
                    uint32_t ip, uint16_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = port;
    
    ssize_t sent = sendto(fd, msg, len, 0,
                          (struct sockaddr *)&addr, sizeof(addr));
    
    return (sent == (ssize_t)len) ? 0 : -1;
}

/**
 * @brief Receive a handshake message via UDP with timeout.
 *
 * @param fd UDP socket
 * @param out Output buffer
 * @param outcap Buffer capacity
 * @param from_ip Output: sender IP (network byte order)
 * @param from_port Output: sender port (network byte order)
 * @param timeout_ms Timeout in milliseconds
 * @return Bytes received, or -1 on error/timeout
 */
static int udp_recv(int fd, unsigned char *out, size_t outcap,
                    uint32_t *from_ip, uint16_t *from_port,
                    int timeout_ms) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    /* Set receive timeout */
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    ssize_t n = recvfrom(fd, out, outcap, 0,
                         (struct sockaddr *)&addr, &addr_len);
    
    if (n < 0) return -1;
    
    if (from_ip) *from_ip = addr.sin_addr.s_addr;
    if (from_port) *from_port = addr.sin_port;
    
    return (int)n;
}

/**
 * @brief Perform complete initiator handshake (blocking).
 *
 * Sends INIT, waits for RESP, sends CONFIRM.
 * Blocks until handshake completes or times out.
 *
 * @param session Session handle (must be initiator)
 * @param self_pub Our public key
 * @param self_secret Our secret key
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success (session established), -1 on error
 */
int norn_session_handshake_initiator(norn_session_t *session,
                                      const unsigned char *self_pub,
                                      const unsigned char *self_secret,
                                      int timeout_ms) {
    if (!session || !self_pub || !self_secret) return -1;
    if (!session->is_initiator) return -1;
    if (session->fd < 0) return -1;
    if (session->peer_ip == 0) return -1;
    
    /* Set our identity */
    norn_session_set_identity(session, self_pub, self_secret);
    
    /* Generate ephemeral key */
    if (channel_gen_ephemeral(&session->channel) != 0) return -1;
    
    /* Build INIT */
    unsigned char init_msg[CHANNEL_INIT_LEN];
    int init_len = norn_session_build_init(session, init_msg, sizeof(init_msg));
    if (init_len < 0) return -1;
    
    /* Send INIT */
    if (udp_send(session->fd, init_msg, init_len,
                  session->peer_ip, session->peer_port) != 0) {
        return -1;
    }
    
    /* Wait for RESP */
    unsigned char resp_msg[CHANNEL_RESP_LEN];
    uint32_t from_ip;
    uint16_t from_port;
    int resp_len = udp_recv(session->fd, resp_msg, sizeof(resp_msg),
                             &from_ip, &from_port, timeout_ms);
    if (resp_len < 0) return -1;
    
    /* Verify responder matches expected peer */
    if (from_ip != session->peer_ip || from_port != session->peer_port) {
        return -1;  /* Wrong peer */
    }
    
    /* Handle RESP and build CONFIRM */
    unsigned char confirm_msg[CHANNEL_CONFIRM_LEN];
    int confirm_len = norn_session_confirm_resp(session, resp_msg, resp_len,
                                                 confirm_msg, sizeof(confirm_msg));
    if (confirm_len < 0) return -1;
    
    /* Send CONFIRM */
    if (udp_send(session->fd, confirm_msg, confirm_len,
                  session->peer_ip, session->peer_port) != 0) {
        return -1;
    }
    
    /* Session is now established */
    session->state = NORN_SESSION_ESTABLISHED;
    
    return 0;
}

/**
 * @brief Perform complete responder handshake (blocking).
 *
 * Waits for INIT, sends RESP, waits for CONFIRM.
 * Blocks until handshake completes or times out.
 *
 * @param session Session handle (must be responder)
 * @param self_pub Our public key
 * @param self_secret Our secret key
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success (session established), -1 on error
 */
int norn_session_handshake_responder(norn_session_t *session,
                                      const unsigned char *self_pub,
                                      const unsigned char *self_secret,
                                      int timeout_ms) {
    if (!session || !self_pub || !self_secret) return -1;
    if (session->is_initiator) return -1;
    if (session->fd < 0) return -1;
    
    /* Set our identity */
    norn_session_set_identity(session, self_pub, self_secret);
    
    /* Generate ephemeral key */
    if (channel_gen_ephemeral(&session->channel) != 0) return -1;
    
    /* Wait for INIT */
    unsigned char init_msg[CHANNEL_INIT_LEN];
    uint32_t from_ip;
    uint16_t from_port;
    int init_len = udp_recv(session->fd, init_msg, sizeof(init_msg),
                             &from_ip, &from_port, timeout_ms);
    if (init_len < 0) return -1;
    
    /* Store peer endpoint */
    session->peer_ip = from_ip;
    session->peer_port = from_port;
    
    /* Handle INIT and build RESP */
    unsigned char resp_msg[CHANNEL_RESP_LEN];
    int resp_len = norn_session_accept_init(session, init_msg, init_len,
                                             resp_msg, sizeof(resp_msg));
    if (resp_len < 0) return -1;
    
    /* Send RESP */
    if (udp_send(session->fd, resp_msg, resp_len,
                  session->peer_ip, session->peer_port) != 0) {
        return -1;
    }
    
    /* Wait for CONFIRM */
    unsigned char confirm_msg[CHANNEL_CONFIRM_LEN];
    int confirm_len = udp_recv(session->fd, confirm_msg, sizeof(confirm_msg),
                                NULL, NULL, timeout_ms);
    if (confirm_len < 0) return -1;
    
    /* Verify and finish */
    if (norn_session_finish_confirm(session, confirm_msg, confirm_len) != 0) {
        return -1;
    }
    
    /* Session is now established */
    session->state = NORN_SESSION_ESTABLISHED;
    
    return 0;
}

/**
 * @brief Create a bound UDP socket for listening.
 *
 * @param port Port to bind (0 for auto-assign, network byte order)
 * @return Socket fd, or -1 on error
 */
int norn_udp_listen(uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = port;
    
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    
    return fd;
}

/**
 * @brief Get local port of bound socket.
 *
 * @param fd Socket fd
 * @return Port (network byte order), or 0 on error
 */
uint16_t norn_udp_get_port(int fd) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0) {
        return 0;
    }
    
    return addr.sin_port;
}

/**
 * @brief Create UDP socket connected to a peer.
 *
 * @param ip Peer IP (network byte order)
 * @param port Peer port (network byte order)
 * @return Socket fd, or -1 on error
 */
int norn_udp_connect(uint32_t ip, uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = port;
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    
    return fd;
}