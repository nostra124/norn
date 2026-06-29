#include "norn_upnp.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SSDP_ADDR "239.255.255.250"
#define SSDP_PORT 1900
#define UPNP_RX_TIMEOUT 3
#define MAX_RESPONSE 8192
#define MAX_URL 512

/* ---- helpers ---- */

static int udp_recv_timeout(int fd, char *buf, size_t cap,
                            struct sockaddr_in *from, int sec) {
    struct timeval tv = {sec, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    socklen_t sl = sizeof(*from);
    ssize_t n = recvfrom(fd, buf, cap - 1, 0, (void*)from, &sl);
    if (n <= 0) return -1;
    buf[n] = 0;
    return (int)n;
}

/* ---- SSDP M-SEARCH ---- */

static int ssdp_discover(char *url_buf, size_t url_cap) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in bindaddr;
    memset(&bindaddr, 0, sizeof(bindaddr));
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = INADDR_ANY;
    bindaddr.sin_port = 0;
    if (bind(fd, (void*)&bindaddr, sizeof(bindaddr)) < 0) { close(fd); return -1; }

    const char *msearch =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
        "\r\n";

    struct sockaddr_in mcast;
    memset(&mcast, 0, sizeof(mcast));
    mcast.sin_family = AF_INET;
    mcast.sin_addr.s_addr = inet_addr(SSDP_ADDR);
    mcast.sin_port = htons(SSDP_PORT);

    if (sendto(fd, msearch, strlen(msearch), 0,
               (void*)&mcast, sizeof(mcast)) < 0) { close(fd); return -1; }

    char resp[MAX_RESPONSE];
    struct sockaddr_in from;
    int n = udp_recv_timeout(fd, resp, sizeof(resp), &from, UPNP_RX_TIMEOUT);
    close(fd);
    if (n <= 0) return -1;

    /* Extract LOCATION header */
    const char *loc = strstr(resp, "LOCATION:");
    if (!loc) loc = strstr(resp, "Location:");
    if (!loc) loc = strstr(resp, "location:");
    if (!loc) return -1;
    loc += 9;
    while (*loc == ' ') loc++;
    const char *end = strchr(loc, '\r');
    if (!end) end = strchr(loc, '\n');
    if (!end) return -1;
    size_t len = end - loc;
    if (len >= url_cap) len = url_cap - 1;
    memcpy(url_buf, loc, len);
    url_buf[len] = 0;
    return 0;
}

/* ---- simple HTTP GET (for fetching device/service descriptions) ---- */

static int http_get(const char *url, char *buf, size_t cap) {
    /* Parse host:port from URL: http://host:port/path */
    const char *p = url;
    if (strncmp(p, "http://", 7) != 0) return -1;
    p += 7;
    char host[256];
    int port = 80;
    const char *slash = strchr(p, '/');
    if (!slash) return -1;
    size_t hlen = slash - p;
    if (hlen >= sizeof(host)) return -1;
    memcpy(host, p, hlen);
    host[hlen] = 0;
    const char *colon = memchr(host, ':', hlen);
    if (colon) {
        port = atoi(colon + 1);
        host[colon - host] = 0;
    }

    uint32_t ip = net_resolve(host);
    if (ip == 0) return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = ip;
    sa.sin_port = htons(port);

    struct timeval tv = {UPNP_RX_TIMEOUT, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, (void*)&sa, sizeof(sa)) < 0) { close(fd); return -1; }

    char req[1024];
    int rn = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
        slash, host);
    if (rn <= 0) { close(fd); return -1; }

    if (send(fd, req, rn, 0) < 0) { close(fd); return -1; }

    /* Read whole response into buf, skip headers */
    char raw[MAX_RESPONSE];
    int total = 0;
    while (total < (int)sizeof(raw) - 1) {
        int n = (int)read(fd, raw + total, sizeof(raw) - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    close(fd);
    if (total <= 0) return -1;
    raw[total] = 0;

    /* Find blank line separating headers and body */
    char *body = strstr(raw, "\r\n\r\n");
    if (body) body += 4;
    else {
        body = strstr(raw, "\n\n");
        if (body) body += 2;
        else return -1;
    }
    size_t blen = strlen(body);
    if (blen >= cap) blen = cap - 1;
    memcpy(buf, body, blen);
    buf[blen] = 0;
    return (int)blen;
}

/* ---- extract a simple XML tag value ---- */

static int xml_get(const char *xml, const char *tag, char *out, size_t cap) {
    char open[128];
    snprintf(open, sizeof(open), "<%s>", tag);
    char close[128];
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *s = strstr(xml, open);
    if (!s) return -1;
    s += strlen(open);
    const char *e = strstr(s, close);
    if (!e) return -1;
    size_t len = e - s;
    if (len >= cap) len = cap - 1;
    memcpy(out, s, len);
    out[len] = 0;
    return (int)len;
}

/* ---- UPnP SOAP call ---- */

static int soap_action(const char *device_url,
                       const char *service_type,
                       const char *ctrl_url,
                       const char *action,
                       const char *soap_body,
                       char *resp_buf, size_t resp_cap) {
    /* Reconstruct full URL for the control endpoint */
    char ctrl_full[MAX_URL];
    {
        const char *p = device_url;
        const char *slash = strchr(p + 8, '/');  /* after http:// */
        if (!slash) return -1;
        size_t base_len = slash - p;
        if (base_len + strlen(ctrl_url) + 1 >= MAX_URL) return -1;
        memcpy(ctrl_full, p, base_len);
        memcpy(ctrl_full + base_len, ctrl_url, strlen(ctrl_url) + 1);
    }

    /* Build HTTP POST with SOAP envelope */
    char host[256];
    const char *hstart = ctrl_full + 7; /* after http:// */
    const char *hslash_pos = strchr(hstart, '/');
    size_t hlen = hslash_pos ? (size_t)(hslash_pos - hstart) : strlen(hstart);
    if (hlen >= sizeof(host)) return -1;
    memcpy(host, hstart, hlen);
    host[hlen] = 0;

    char soap[4096];
    int n = snprintf(soap, sizeof(soap),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPAction: \"%s#%s\"\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        hslash_pos ? hslash_pos : "/",
        host,
        service_type, action,
        strlen(soap_body), soap_body);
    if (n <= 0) return -1;

    /* Send SOAP POST to the device */
    {
        uint32_t ip = net_resolve(host);
        if (ip == 0) return -1;

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = ip;
        const char *colon = memchr(host, ':', hlen);
        sa.sin_port = htons(colon ? atoi(colon + 1) : 80);

        struct timeval tv = {UPNP_RX_TIMEOUT, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, (void*)&sa, sizeof(sa)) < 0) { close(fd); return -1; }
        if (send(fd, soap, n, 0) < 0) { close(fd); return -1; }

        char raw[MAX_RESPONSE];
        int total = 0;
        while (total < (int)sizeof(raw) - 1) {
            int r = (int)read(fd, raw + total, sizeof(raw) - 1 - total);
            if (r <= 0) break;
            total += r;
        }
        close(fd);
        if (total <= 0) return -1;
        raw[total] = 0;

        char *body = strstr(raw, "\r\n\r\n");
        if (body) body += 4;
        else {
            body = strstr(raw, "\n\n");
            if (body) body += 2;
            else return -1;
        }
        size_t blen = strlen(body);
        if (blen >= resp_cap) blen = resp_cap - 1;
        memcpy(resp_buf, body, blen);
        resp_buf[blen] = 0;
    }
    return 0;
}

/* ---- public UPnP API ---- */

int norn_upnp_discover(uint32_t timeout_ms, char *device_url) {
    (void)timeout_ms;
    return ssdp_discover(device_url, MAX_URL);
}

int norn_upnp_add_mapping(const char *device_url,
                           uint16_t internal_port,
                           uint16_t external_port,
                           const char *protocol,
                           uint32_t lease_duration,
                           norn_upnp_result_t *result) {
    if (!device_url || !result) return -1;

    /* Fetch device description */
    char desc[MAX_RESPONSE];
    if (http_get(device_url, desc, sizeof(desc)) < 0) return -1;

    /* Find WANIPConnection or WANPPPConnection service */
    const char *svc_types[] = {
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
        NULL
    };
    char svc_type[256] = "";
    char ctrl_url[256] = "";
    for (int i = 0; svc_types[i]; i++) {
        if (strstr(desc, svc_types[i])) {
            strncpy(svc_type, svc_types[i], sizeof(svc_type) - 1);
            /* Extract controlURL from the service element */
            const char *svc = strstr(desc, svc_types[i]);
            if (svc) {
                const char *cu = strstr(svc, "<controlURL>");
                if (cu) {
                    cu += 11;
                    const char *ce = strstr(cu, "</controlURL>");
                    if (ce) {
                        size_t cl = ce - cu;
                        if (cl < sizeof(ctrl_url)) {
                            memcpy(ctrl_url, cu, cl);
                            ctrl_url[cl] = 0;
                        }
                    }
                }
            }
            break;
        }
    }
    if (!svc_type[0] || !ctrl_url[0]) return -1;

    /* Build SOAP AddPortMapping envelope */
    char soap_body[2048];
    int n = snprintf(soap_body, sizeof(soap_body),
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:AddPortMapping xmlns:u=\"%s\">"
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>%u</NewExternalPort>"
        "<NewProtocol>%s</NewProtocol>"
        "<NewInternalPort>%u</NewInternalPort>"
        "<NewInternalClient>%s</NewInternalClient>"
        "<NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>norn dht</NewPortMappingDescription>"
        "<NewLeaseDuration>%u</NewLeaseDuration>"
        "</u:AddPortMapping>"
        "</s:Body>"
        "</s:Envelope>",
        svc_type,
        (unsigned int)(external_port ? external_port : internal_port),
        protocol,
        (unsigned int)internal_port,
        inet_ntoa(*(struct in_addr*)&(uint32_t){net_local_ip()}),
        (unsigned int)lease_duration);
    if (n <= 0) return -1;

    char resp[MAX_RESPONSE];
    if (soap_action(device_url, svc_type, ctrl_url,
                    "AddPortMapping", soap_body,
                    resp, sizeof(resp)) < 0) return -1;

    /* Extract external IP from response */
    char ext_ip_str[64];
    result->success = 0;
    if (xml_get(resp, "NewExternalIPAddress", ext_ip_str, sizeof(ext_ip_str)) == 0) {
        result->external_ip = inet_addr(ext_ip_str);
    } else {
        /* UPnP may not return it; try GetExternalIPAddress */
        result->external_ip = 0;
    }
    result->external_port = htons(external_port ? external_port : internal_port);
    result->internal_port = htons(internal_port);
    result->lease_duration = lease_duration;
    result->success = 1;
    return 0;
}

int norn_upnp_remove_mapping(const char *device_url,
                              uint16_t external_port,
                              const char *protocol) {
    (void)device_url;
    (void)external_port;
    (void)protocol;
    /* TODO: implement DeletePortMapping if needed */
    return -1;
}

int norn_upnp_get_external_ip(const char *device_url, uint32_t *external_ip) {
    (void)device_url;
    (void)external_ip;
    /* TODO: implement via GetExternalIPAddress SOAP call */
    return -1;
}

int norn_natpmp_add_mapping(uint32_t gateway_ip,
                             uint16_t internal_port,
                             uint16_t external_port,
                             int protocol,
                             uint32_t lease_duration,
                             norn_upnp_result_t *result) {
    (void)gateway_ip;
    (void)internal_port;
    (void)external_port;
    (void)protocol;
    (void)lease_duration;
    (void)result;
    /* NAT-PMP implemented in net.c via natpmp_map_udp() */
    return -1;
}

int norn_natpmp_remove_mapping(uint32_t gateway_ip,
                                uint16_t external_port,
                                int protocol) {
    (void)gateway_ip;
    (void)external_port;
    (void)protocol;
    return -1;
}

int norn_auto_port_mapping(uint16_t internal_port,
                            const char *protocol,
                            norn_upnp_result_t *result) {
    if (!protocol || !result) return -1;
    result->success = 0;

    /* Try NAT-PMP first (simpler, faster, works on most modern routers) */
    {
        uint16_t ext_port;
        uint32_t ext_ip;
        if (natpmp_map_udp(internal_port, 3600, &ext_port, &ext_ip) == 0) {
            result->external_ip = ext_ip;
            result->external_port = htons(ext_port);
            result->internal_port = htons(internal_port);
            result->lease_duration = 3600;
            result->success = 1;
            return 0;
        }
    }

    /* Try UPnP as fallback */
    {
        char device_url[256];
        if (norn_upnp_discover(5000, device_url) == 0) {
            if (norn_upnp_add_mapping(device_url, internal_port, 0,
                                       protocol, 3600, result) == 0) {
                if (result->success) return 0;
            }
        }
    }

    return -1;
}
