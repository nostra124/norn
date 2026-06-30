/* SPDX-License-Identifier: MIT */
#include "net.h"
#include "log.h"   /* BUG-113: surface socket/bind errno on the daemon's main UDP socket */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#elif defined(__APPLE__) || defined(__unix__)
#include <net/route.h>
#endif
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>

int net_init(net_t *net, uint16_t port) {
    if (!net) return -1;
    
    memset(net, 0, sizeof(net_t));
    net->port = port;
    net->external_ip_valid = 0;
    
    net->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (net->fd < 0) { LOGE("net: socket() failed: %s", strerror(errno)); return -1; }
    
    int flags = fcntl(net->fd, F_GETFL, 0);
    fcntl(net->fd, F_SETFL, flags | O_NONBLOCK);
    
    int opt = 1;
    setsockopt(net->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(net->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGE("net: bind UDP port %u failed: %s", port, strerror(errno));
        close(net->fd);
        return -1;
    }
    
    socklen_t addr_len = sizeof(addr);
    if (getsockname(net->fd, (struct sockaddr *)&addr, &addr_len) == 0) {
        net->port = ntohs(addr.sin_port);
    }
    
    return 0;
}

uint16_t net_get_bound_port(net_t *net) {
    if (!net || net->fd < 0) return 0;
    return net->port;
}

void net_cleanup(net_t *net) {
    if (!net) return;
    if (net->fd >= 0) {
        close(net->fd);
        net->fd = -1;
    }
}

int net_send(net_t *net, const uint8_t *data, size_t len, uint32_t ip, uint16_t port) {
    if (!net || !data || net->fd < 0) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);
    
    ssize_t sent = sendto(net->fd, data, len, 0, (struct sockaddr *)&addr, sizeof(addr));
    if (sent < 0) return -1;
    
    return (size_t)sent == len ? 0 : -1;
}

int net_recv(net_t *net, uint8_t *buf, size_t buf_len, uint32_t *from_ip, uint16_t *from_port) {
    if (!net || !buf || net->fd < 0) return -1;
    
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    ssize_t recv = recvfrom(net->fd, buf, buf_len, 0, (struct sockaddr *)&from, &from_len);
    if (recv < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    
    if (from_ip) *from_ip = from.sin_addr.s_addr;
    if (from_port) *from_port = ntohs(from.sin_port);
    
    return (int)recv;
}

#define EXT_MAX_VOTES 8
#define EXT_CONFIRM_THRESHOLD 2
#define EXT_TERMINAL_OBS 6   /* observations after which NAT type settles (BUG-064) */

/* Feed one observed reflexive address (from a peer's BEP-42 "ip" field).
 * Votes across observers: the majority endpoint becomes our confirmed public
 * address; divergent ports from the same IP indicate symmetric NAT. */
void net_update_external_ip(net_t *net, uint32_t ip, uint16_t port) {
    if (!net || ip == 0) return;
    if (net->mapped) return;  /* NAT-PMP mapping is authoritative; ignore votes */

    int found = 0;
    for (int i = 0; i < net->ext_vote_count; i++) {
        if (net->ext_votes[i].ip == ip && net->ext_votes[i].port == port) {
            net->ext_votes[i].count++;
            found = 1;
            break;
        }
    }
    if (!found && net->ext_vote_count < EXT_MAX_VOTES) {
        net->ext_votes[net->ext_vote_count].ip = ip;
        net->ext_votes[net->ext_vote_count].port = port;
        net->ext_votes[net->ext_vote_count].count = 1;
        net->ext_vote_count++;
    }

    /* symmetric NAT: same public IP reported with different ports, each backed
     * by more than one observer */
    for (int i = 0; i < net->ext_vote_count; i++) {
        for (int j = i + 1; j < net->ext_vote_count; j++) {
            if (net->ext_votes[i].ip == net->ext_votes[j].ip &&
                net->ext_votes[i].port != net->ext_votes[j].port &&
                net->ext_votes[i].count >= 2 && net->ext_votes[j].count >= 2) {
                net->nat_symmetric = 1;
            }
        }
    }

    /* confirmed endpoint = most-voted bucket once it crosses the threshold */
    int best = -1, besti = -1;
    for (int i = 0; i < net->ext_vote_count; i++) {
        if (net->ext_votes[i].count > best) { best = net->ext_votes[i].count; besti = i; }
    }
    if (besti >= 0 && net->ext_votes[besti].count >= EXT_CONFIRM_THRESHOLD) {
        net->external_ip = net->ext_votes[besti].ip;
        net->external_port = net->ext_votes[besti].port;
        net->external_ip_valid = 1;
    }
}

int net_get_external_ip(net_t *net, uint32_t *ip) {
    if (!net || !ip) return -1;
    if (!net->external_ip_valid) return -1;
    *ip = net->external_ip;
    return 0;
}

int net_get_external_endpoint(net_t *net, uint32_t *ip, uint16_t *port) {
    if (!net || !net->external_ip_valid) return -1;
    if (ip) *ip = net->external_ip;
    if (port) *port = net->external_port;
    return 0;
}

int net_nat_is_symmetric(net_t *net) {
    return net && net->nat_symmetric;
}

/* For `status`: total reflexive observations so far + the current leading candidate
 * (may not yet have crossed the confirm threshold). Returns the total vote count. */
int net_external_progress(net_t *net, uint32_t *lead_ip, uint16_t *lead_port, int *lead_count) {
    if (!net) return 0;
    int total = 0, best = -1, besti = -1;
    for (int i = 0; i < net->ext_vote_count; i++) {
        total += net->ext_votes[i].count;
        if (net->ext_votes[i].count > best) { best = net->ext_votes[i].count; besti = i; }
    }
    if (besti >= 0) {
        if (lead_ip) *lead_ip = net->ext_votes[besti].ip;
        if (lead_port) *lead_port = net->ext_votes[besti].port;
        if (lead_count) *lead_count = net->ext_votes[besti].count;
    }
    return total;
}

/* BUG-078: the primary local IPv4 (first non-loopback UP interface, network order),
 * 0 if none — used to detect a network change. */
uint32_t net_local_ip(void) {
    struct ifaddrs *ifa = NULL, *p;
    uint32_t best = 0;
    if (getifaddrs(&ifa) != 0) return 0;
    for (p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (!(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK)) continue;
        uint32_t ip = ((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr;
        if ((ntohl(ip) >> 24) == 127) continue;
        best = ip; break;
    }
    freeifaddrs(ifa);
    return best;
}

/* BUG-078: forget the learned reflexive endpoint so a network change re-detects it
 * from fresh BEP-42 votes (the old votes never expire and would keep winning). */
void net_reset_external(net_t *net) {
    if (!net) return;
    net->ext_vote_count = 0;
    net->external_ip_valid = 0;
    net->nat_symmetric = 0;
}

/* BUG-078: a socket that becomes readable when the OS reports a network change
 * (interface address/link change) — Linux rtnetlink, macOS/BSD PF_ROUTE — so we
 * re-detect our endpoint INSTANTLY, not just on the next self-poll. Returns -1 on
 * platforms without support (the poll still covers us). */
int net_route_monitor_open(void) {
    int fd = -1;
#if defined(__linux__)
    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd >= 0) {
        struct sockaddr_nl sa; memset(&sa, 0, sizeof(sa));
        sa.nl_family = AF_NETLINK;
        sa.nl_groups = RTMGRP_IPV4_IFADDR | RTMGRP_LINK;
        if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return -1; }
    }
#elif defined(PF_ROUTE)
    fd = socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);   /* macOS/BSD route messages */
#endif
    if (fd >= 0) { int fl = fcntl(fd, F_GETFL, 0); if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK); }
    return fd;
}

/* Discard whatever the OS queued — any message means "something changed"; we re-poll
 * net_local_ip() to decide if it actually affects us. */
void net_route_monitor_drain(int fd) {
    if (fd < 0) return;
    unsigned char buf[4096];
    while (recv(fd, buf, sizeof(buf), 0) > 0) { /* drain */ }
}

const char *net_nat_type(net_t *net) {
    if (!net) return "unknown";
    if (net->mapped) return "open (NAT-PMP)";
    if (net->nat_symmetric) return "symmetric";
    if (!net->external_ip_valid) {
        /* Not confirmed. Rather than say "detecting" forever (BUG-064): once we've
         * gathered enough observations, settle. A symmetric NAT hands each observer
         * a DIFFERENT reflexive port, so the same public IP appears across several
         * count-1 buckets — classify that as symmetric; otherwise "unknown". */
        int total = 0;
        for (int i = 0; i < net->ext_vote_count; i++) total += net->ext_votes[i].count;
        if (total < EXT_TERMINAL_OBS) return "detecting";
        for (int i = 0; i < net->ext_vote_count; i++)
            for (int j = i + 1; j < net->ext_vote_count; j++)
                if (net->ext_votes[i].ip == net->ext_votes[j].ip &&
                    net->ext_votes[i].port != net->ext_votes[j].port)
                    return "symmetric";   /* same IP, divergent ports = symmetric */
        return "unknown";
    }
    /* External endpoint confirmed and no port divergence seen. Only call it
     * "cone" once a single bucket has real corroboration (>=2 observers);
     * otherwise we simply don't have enough evidence yet. */
    int best = 0;
    for (int i = 0; i < net->ext_vote_count; i++)
        if (net->ext_votes[i].count > best) best = net->ext_votes[i].count;
    return best >= 2 ? "cone" : "detecting";
}

void net_set_mapped_endpoint(net_t *net, uint32_t ip, uint16_t port) {
    if (!net || ip == 0) return;
    net->external_ip = ip;
    net->external_port = port;
    net->external_ip_valid = 1;
    net->nat_symmetric = 0;   /* a working NAT-PMP mapping is directly reachable */
    net->mapped = 1;
}

/* ---- NAT-PMP (RFC 6886), dependency-free ---------------------------------- */

/* Find the default-gateway IPv4 address (network byte order). */
static int natpmp_gateway(uint32_t *gw_ip) {
#ifdef __linux__
    FILE *f = fopen("/proc/net/route", "r");
    if (!f) return -1;
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }  /* header */
    while (fgets(line, sizeof(line), f)) {
        char iface[64];
        unsigned long dest = 0, gw = 0, flags = 0;
        if (sscanf(line, "%63s %lx %lx %lx", iface, &dest, &gw, &flags) >= 4) {
            if (dest == 0 && (flags & 0x2 /* RTF_GATEWAY */)) {
                *gw_ip = (uint32_t)gw;  /* already in network byte order */
                fclose(f);
                return 0;
            }
        }
    }
    fclose(f);
    return -1;
#else
    /* BSD/macOS: ask the routing table via the `route` tool */
    FILE *p = popen("route -n get default 2>/dev/null", "r");
    if (!p) return -1;
    char line[256];
    int ok = -1;
    while (fgets(line, sizeof(line), p)) {
        char *g = strstr(line, "gateway:");
        if (g) {
            char ipstr[64];
            if (sscanf(g, "gateway: %63s", ipstr) == 1) {
                struct in_addr a;
                if (inet_aton(ipstr, &a)) { *gw_ip = a.s_addr; ok = 0; }
            }
            break;
        }
    }
    pclose(p);
    return ok;
#endif
}

/* Send req and wait for a response with the expected opcode, with RFC-6886
 * style retries. Returns response length, or -1. */
static int natpmp_xchg(int sock, struct sockaddr_in *gw, const uint8_t *req,
                       size_t reqlen, uint8_t *resp, size_t resplen, uint8_t expect_op) {
    for (int attempt = 0; attempt < 4; attempt++) {
        if (sendto(sock, req, reqlen, 0, (struct sockaddr *)gw, sizeof(*gw)) < 0)
            return -1;
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250000 * (attempt + 1);  /* 0.25s, 0.5s, 0.75s, 1s */
        fd_set rf; FD_ZERO(&rf); FD_SET(sock, &rf);
        if (select(sock + 1, &rf, NULL, NULL, &tv) <= 0) continue;
        struct sockaddr_in from; socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(sock, resp, resplen, 0, (struct sockaddr *)&from, &fl);
        if (n >= 2 && from.sin_addr.s_addr == gw->sin_addr.s_addr &&
            resp[0] == 0 && resp[1] == expect_op) {
            return (int)n;
        }
    }
    return -1;
}

int natpmp_map_udp(uint16_t internal_port, uint32_t lifetime,
                   uint16_t *external_port, uint32_t *external_ip) {
    uint32_t gw_ip;
    if (natpmp_gateway(&gw_ip) != 0) return -1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in gw;
    memset(&gw, 0, sizeof(gw));
    gw.sin_family = AF_INET;
    gw.sin_addr.s_addr = gw_ip;
    gw.sin_port = htons(5351);

    /* 1) external address request: {version=0, op=0} -> op 128 reply */
    uint8_t req0[2] = { 0, 0 };
    uint8_t r0[16];
    int n = natpmp_xchg(sock, &gw, req0, sizeof(req0), r0, sizeof(r0), 128);
    uint16_t result;
    if (n < 12) { close(sock); return -1; }
    memcpy(&result, r0 + 2, 2); result = ntohs(result);
    if (result != 0) { close(sock); return -1; }
    uint32_t ext_ip;
    memcpy(&ext_ip, r0 + 8, 4);  /* network byte order */

    /* 2) map UDP request: {0, 1, reserved(2), internal(2), suggested(2), lifetime(4)} */
    uint8_t req1[12];
    req1[0] = 0; req1[1] = 1; req1[2] = 0; req1[3] = 0;
    uint16_t ip_be = htons(internal_port);
    memcpy(req1 + 4, &ip_be, 2);
    memcpy(req1 + 6, &ip_be, 2);  /* suggest external == internal */
    uint32_t lt_be = htonl(lifetime);
    memcpy(req1 + 8, &lt_be, 4);
    uint8_t r1[16];
    n = natpmp_xchg(sock, &gw, req1, sizeof(req1), r1, sizeof(r1), 129);
    close(sock);
    if (n < 16) return -1;
    memcpy(&result, r1 + 2, 2); result = ntohs(result);
    if (result != 0) return -1;
    uint16_t mapped;
    memcpy(&mapped, r1 + 10, 2); mapped = ntohs(mapped);  /* assigned external port */
    if (mapped == 0) return -1;

    if (external_port) *external_port = mapped;
    if (external_ip) *external_ip = ext_ip;
    return 0;
}

uint32_t net_resolve(const char *hostname) {
    if (!hostname) return 0;

    struct hostent *he = gethostbyname(hostname);
    /* Validate before dereferencing: gethostbyname can return an entry with an
     * empty or non-IPv4 (AAAA-only) address list; the old `*(uint32_t*)
     * h_addr_list[0]` then crashed / read unaligned. memcpy, not a pointer cast.
     * Callers already treat 0 as "unresolved" (BUG-031). */
    if (!he || he->h_addrtype != AF_INET || he->h_length < 4 ||
        !he->h_addr_list || !he->h_addr_list[0])
        return 0;

    uint32_t ip;
    memcpy(&ip, he->h_addr_list[0], 4);
    return ip;
}

const char *net_ip_to_str(uint32_t ip) {
    struct in_addr addr;
    addr.s_addr = ip;
    return inet_ntoa(addr);
}