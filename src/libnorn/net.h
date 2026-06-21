#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int fd;
    uint32_t external_ip;       /* public IP as observed via the BEP-42 ip field */
    uint16_t port;              /* local (bound) UDP port */
    uint16_t external_port;     /* public port as observed via the BEP-42 ip field */
    int external_ip_valid;      /* set once a reflexive address reaches the vote threshold */
    int nat_symmetric;          /* set if observed ports diverge (symmetric NAT) */
    int mapped;                 /* set when the public endpoint came from NAT-PMP (authoritative) */
    struct { uint32_t ip; uint16_t port; int count; } ext_votes[8];
    int ext_vote_count;
} net_t;

int net_init(net_t *net, uint16_t port);
uint16_t net_get_bound_port(net_t *net);
void net_cleanup(net_t *net);

int net_send(net_t *net, const uint8_t *data, size_t len, uint32_t ip, uint16_t port);
int net_recv(net_t *net, uint8_t *buf, size_t buf_len, uint32_t *from_ip, uint16_t *from_port);

void net_update_external_ip(net_t *net, uint32_t ip, uint16_t port);
int net_get_external_ip(net_t *net, uint32_t *ip);
/* BUG-078: primary local IPv4 (network order, 0 if none) + forget the learned
 * reflexive endpoint, so a network change re-detects the public endpoint. */
uint32_t net_local_ip(void);
void net_reset_external(net_t *net);
/* BUG-078: OS network-change notification socket (rtnetlink / PF_ROUTE), readable on
 * an interface/route change; -1 if unsupported. Drain discards queued messages. */
int net_route_monitor_open(void);
void net_route_monitor_drain(int fd);
/* status helper: total reflexive votes so far + the leading (maybe-unconfirmed)
 * candidate; returns the total vote count. */
int net_external_progress(net_t *net, uint32_t *lead_ip, uint16_t *lead_port, int *lead_count);
/* Returns 0 and fills the confirmed public endpoint (majority-voted reflexive
 * address), or -1 if not yet confirmed. */
int net_get_external_endpoint(net_t *net, uint32_t *ip, uint16_t *port);
int net_nat_is_symmetric(net_t *net);
/* Human NAT classification: "open (NAT-PMP)", "symmetric", "cone", or
 * "detecting" when there isn't yet enough reflexive evidence. Unlike
 * net_nat_is_symmetric(), this never reports a positive "cone" until corroborated,
 * so a young daemon says "detecting" rather than falsely "cone". */
const char *net_nat_type(net_t *net);
/* Record an authoritative public endpoint obtained from NAT-PMP. */
void net_set_mapped_endpoint(net_t *net, uint32_t ip, uint16_t port);

/* NAT-PMP (RFC 6886): ask the default gateway to map our UDP internal_port for
 * `lifetime` seconds. On success returns 0 and fills the external ip:port the
 * router assigned. Returns -1 if no NAT-PMP gateway / mapping failed. */
int natpmp_map_udp(uint16_t internal_port, uint32_t lifetime,
                   uint16_t *external_port, uint32_t *external_ip);

uint32_t net_resolve(const char *hostname);
const char *net_ip_to_str(uint32_t ip);

#endif