/* SPDX-License-Identifier: MIT */
/**
 * @file norn_upnp.h
 * @brief UPnP/NAT-PMP automatic port forwarding.
 *
 * Provides automatic port forwarding for NAT traversal:
 * - UPnP: Universal Plug and Play (most routers)
 * - NAT-PMP: Apple's simpler protocol
 *
 * When enabled, norn will automatically request port mapping
 * from the router, making the node publicly reachable.
 */

#ifndef NORN_UPNP_H
#define NORN_UPNP_H

#include <stdint.h>

/**
 * @brief UPnP/NAT-PMP result
 */
typedef struct {
    uint32_t external_ip;     /* External IP (network byte order) */
    uint16_t external_port;   /* External port (network byte order) */
    uint16_t internal_port;  /* Internal port (network byte order) */
    uint32_t lease_duration;  /* Lease duration in seconds */
    int success;             /* 1 on success, 0 on failure */
} norn_upnp_result_t;

/**
 * @brief Discover UPnP devices on the network.
 *
 * @param timeout_ms Discovery timeout in milliseconds
 * @param device_url Output buffer for device URL (256 bytes)
 * @return 0 on success, -1 on error
 */
int norn_upnp_discover(uint32_t timeout_ms, char *device_url);

/**
 * @brief Request port mapping via UPnP.
 *
 * @param device_url Device URL from discovery
 * @param internal_port Internal port to map
 * @param external_port Desired external port (0 for any)
 * @param protocol "TCP" or "UDP"
 * @param lease_duration Lease duration in seconds
 * @param result Output result
 * @return 0 on success, -1 on error
 */
int norn_upnp_add_mapping(const char *device_url,
                          uint16_t internal_port,
                          uint16_t external_port,
                          const char *protocol,
                          uint32_t lease_duration,
                          norn_upnp_result_t *result);

/**
 * @brief Remove port mapping via UPnP.
 *
 * @param device_url Device URL from discovery
 * @param external_port External port to unmap
 * @param protocol "TCP" or "UDP"
 * @return 0 on success, -1 on error
 */
int norn_upnp_remove_mapping(const char *device_url,
                             uint16_t external_port,
                             const char *protocol);

/**
 * @brief Get external IP via UPnP.
 *
 * @param device_url Device URL from discovery
 * @param external_ip Output external IP
 * @return 0 on success, -1 on error
 */
int norn_upnp_get_external_ip(const char *device_url,
                              uint32_t *external_ip);

/**
 * @brief Request port mapping via NAT-PMP.
 *
 * Simpler than UPnP, used by Apple routers.
 *
 * @param gateway_ip Gateway IP (network byte order)
 * @param internal_port Internal port to map
 * @param external_port Desired external port (0 for any)
 * @param protocol 1=UDP, 2=TCP
 * @param lease_duration Lease duration in seconds
 * @param result Output result
 * @return 0 on success, -1 on error
 */
int norn_natpmp_add_mapping(uint32_t gateway_ip,
                            uint16_t internal_port,
                            uint16_t external_port,
                            int protocol,
                            uint32_t lease_duration,
                            norn_upnp_result_t *result);

/**
 * @brief Remove port mapping via NAT-PMP.
 *
 * @param gateway_ip Gateway IP (network byte order)
 * @param external_port External port to unmap
 * @param protocol 1=UDP, 2=TCP
 * @return 0 on success, -1 on error
 */
int norn_natpmp_remove_mapping(uint32_t gateway_ip,
                               uint16_t external_port,
                               int protocol);

/**
 * @brief Try both UPnP and NAT-PMP to get external port.
 *
 * Attempts UPnP first, then NAT-PMP as fallback.
 *
 * @param internal_port Internal port to map
 * @param protocol "TCP" or "UDP"
 * @param result Output result
 * @return 0 on success, -1 on error
 */
int norn_auto_port_mapping(uint16_t internal_port,
                            const char *protocol,
                            norn_upnp_result_t *result);

#endif /* NORN_UPNP_H */