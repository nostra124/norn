/**
 * @file norn_upnp.c
 * @brief UPnP/NAT-PMP automatic port forwarding.
 *
 * Provides automatic port forwarding for NAT traversal.
 * This is the FIRST choice before hole punching or relays.
 */

#include "norn_upnp.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>

#ifdef __FreeBSD__
#include <sys/socket.h>
#endif

#ifdef __APPLE__
#include <sys/socket.h>
#endif

#ifdef __linux__
#include <sys/socket.h>
#endif

/* UPnP discovery and mapping (simplified implementation) */
/* Full UPnP requires HTTP/SSDP which is complex. This is a stub. */

int norn_upnp_discover(uint32_t timeout_ms, char *device_url) {
    /* TODO: Implement UPnP discovery via SSDP
     * - Send M-SEARCH to 239.255.255.250:1900
     * - Listen for UPnP device responses
     * - Extract device URL from response
     * 
     * For now, return error to indicate not implemented
     */
    (void)timeout_ms;
    (void)device_url;
    return -1;
}

int norn_upnp_add_mapping(const char *device_url,
                          uint16_t internal_port,
                          uint16_t external_port,
                          const char *protocol,
                          uint32_t lease_duration,
                          norn_upnp_result_t *result) {
    /* TODO: Implement UPnP AddPortMapping
     * - Send SOAP request to device_url
     * - Request: AddPortMapping(ExternalPort, InternalPort, Protocol, InternalClient, LeaseDuration)
     * - Parse response for external IP/port
     * 
     * For now, return error to indicate not implemented
     */
    (void)device_url;
    (void)internal_port;
    (void)external_port;
    (void)protocol;
    (void)lease_duration;
    (void)result;
    return -1;
}

int norn_upnp_remove_mapping(const char *device_url,
                             uint16_t external_port,
                             const char *protocol) {
    /* TODO: Implement UPnP DeletePortMapping
     * - Send SOAP request to device_url
     * - Request: DeletePortMapping(ExternalPort, Protocol)
     * 
     * For now, return error to indicate not implemented
     */
    (void)device_url;
    (void)external_port;
    (void)protocol;
    return -1;
}

int norn_upnp_get_external_ip(const char *device_url, uint32_t *external_ip) {
    /* TODO: Implement UPnP GetExternalIPAddress
     * - Send SOAP request to device_url
     * - Request: GetExternalIPAddress()
     * - Parse response for external IP
     * 
     * For now, return error to indicate not implemented
     */
    (void)device_url;
    (void)external_ip;
    return -1;
}

/* NAT-PMP implementation (simpler than UPnP) */
/* NAT-PMP uses UDP to gateway on port 5351 */

int norn_natpmp_add_mapping(uint32_t gateway_ip,
                            uint16_t internal_port,
                            uint16_t external_port,
                            int protocol,
                            uint32_t lease_duration,
                            norn_upnp_result_t *result) {
    /* TODO: Implement NAT-PMP mapping request
     * - Send UDP packet to gateway:5351
     * - Request format:
     *   - Version: 0
     *   - Opcode: 1 (MAP UDP) or 2 (MAP TCP)
     *   - Reserved: 0
     *   - Internal port
     *   - External port (0 for any)
     *   - Lease duration
     * - Parse response for external port
     * 
     * For now, return error to indicate not implemented
     */
    (void)gateway_ip;
    (void)internal_port;
    (void)external_port;
    (void)protocol;
    (void)lease_duration;
    (void)result;
    return -1;
}

int norn_natpmp_remove_mapping(uint32_t gateway_ip,
                               uint16_t external_port,
                               int protocol) {
    /* TODO: Implement NAT-PMP unmapping request
     * - Send UDP packet to gateway:5351
     * - Set lease duration to 0
     * 
     * For now, return error to indicate not implemented
     */
    (void)gateway_ip;
    (void)external_port;
    (void)protocol;
    return -1;
}

int norn_auto_port_mapping(uint16_t internal_port,
                            const char *protocol,
                            norn_upnp_result_t *result) {
    if (!protocol || !result) return -1;
    
    /* Try UPnP first (most common) */
    char device_url[256];
    if (norn_upnp_discover(5000, device_url) == 0) {
        if (norn_upnp_add_mapping(device_url, internal_port, 0, protocol, 3600, result) == 0) {
            result->success = 1;
            return 0;
        }
    }
    
    /* Try NAT-PMP as fallback (Apple routers) */
    /* Get gateway IP from routing table */
    uint32_t gateway_ip = 0;
    /* TODO: Implement gateway IP detection */
    if (gateway_ip != 0) {
        int proto = (strcmp(protocol, "UDP") == 0) ? 1 : 2;
        if (norn_natpmp_add_mapping(gateway_ip, internal_port, 0, proto, 3600, result) == 0) {
            result->success = 1;
            return 0;
        }
    }
    
    /* Neither worked */
    result->success = 0;
    return -1;
}