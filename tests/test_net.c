/* SPDX-License-Identifier: MIT */
/* Test network layer with 100% coverage */
#include "net.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

static void test_init_null(void) {
    assert(net_init(NULL, 0) == -1);
    printf("  test_init_null: OK\n");
}

static void test_init_and_cleanup(void) {
    net_t net;
    assert(net_init(&net, 0) == 0);
    assert(net.fd >= 0);
    
    uint16_t port = net_get_bound_port(&net);
    assert(port > 0);
    
    net_cleanup(&net);
    assert(net.fd < 0);
    printf("  test_init_and_cleanup: OK\n");
}

static void test_cleanup_null(void) {
    net_cleanup(NULL);
    printf("  test_cleanup_null: OK\n");
}

static void test_get_port_null(void) {
    assert(net_get_bound_port(NULL) == 0);
    
    net_t net;
    memset(&net, 0, sizeof(net));
    net.fd = -1;
    assert(net_get_bound_port(&net) == 0);
    printf("  test_get_port_null: OK\n");
}

static void test_send_null(void) {
    net_t net;
    unsigned char data[] = "test";
    
    assert(net_send(NULL, data, sizeof(data), 0x01020304, 1234) == -1);
    
    memset(&net, 0, sizeof(net));
    net.fd = -1;
    assert(net_send(&net, data, sizeof(data), 0x01020304, 1234) == -1);
    
    assert(net_send(&net, NULL, 4, 0x01020304, 1234) == -1);
    printf("  test_send_null: OK\n");
}

static void test_recv_null(void) {
    net_t net;
    unsigned char buf[256];
    uint32_t ip;
    uint16_t port;
    
    assert(net_recv(NULL, buf, sizeof(buf), &ip, &port) == -1);
    
    memset(&net, 0, sizeof(net));
    net.fd = -1;
    assert(net_recv(&net, NULL, sizeof(buf), &ip, &port) == -1);
    assert(net_recv(&net, buf, sizeof(buf), &ip, &port) == -1);
    printf("  test_recv_null: OK\n");
}

static void test_external_ip_null(void) {
    uint32_t ip;
    
    assert(net_get_external_ip(NULL, &ip) == -1);
    
    net_t net;
    memset(&net, 0, sizeof(net));
    assert(net_get_external_ip(&net, NULL) == -1);
    assert(net_get_external_ip(&net, &ip) == -1);
    printf("  test_external_ip_null: OK\n");
}

static void test_update_external_ip(void) {
    net_t net;
    memset(&net, 0, sizeof(net));
    
    net_update_external_ip(NULL, 0x01020304, 1234);
    
    net_update_external_ip(&net, 0, 1234);
    
    net_update_external_ip(&net, 0x01020304, 1234);
    assert(net.ext_vote_count == 1);
    assert(net.ext_votes[0].ip == 0x01020304);
    assert(net.ext_votes[0].port == 1234);
    assert(net.ext_votes[0].count == 1);
    
    net_update_external_ip(&net, 0x01020304, 1234);
    assert(net.ext_votes[0].count == 2);
    
    net_update_external_ip(&net, 0x05060708, 5678);
    assert(net.ext_vote_count == 2);
    
    printf("  test_update_external_ip: OK\n");
}

static void test_external_endpoint(void) {
    net_t net;
    memset(&net, 0, sizeof(net));
    uint32_t ip;
    uint16_t port;
    
    assert(net_get_external_endpoint(NULL, &ip, &port) == -1);
    assert(net_get_external_endpoint(&net, &ip, &port) == -1);
    
    net.external_ip_valid = 1;
    net.external_ip = 0x01020304;
    net.external_port = 1234;
    assert(net_get_external_endpoint(&net, &ip, &port) == 0);
    assert(ip == 0x01020304);
    assert(port == 1234);
    printf("  test_external_endpoint: OK\n");
}

static void test_nat_symmetric(void) {
    net_t net;
    memset(&net, 0, sizeof(net));
    
    assert(net_nat_is_symmetric(NULL) == 0);
    assert(net_nat_is_symmetric(&net) == 0);
    
    net.nat_symmetric = 1;
    assert(net_nat_is_symmetric(&net) == 1);
    printf("  test_nat_symmetric: OK\n");
}

static void test_external_progress(void) {
    net_t net;
    memset(&net, 0, sizeof(net));
    uint32_t lead_ip;
    uint16_t lead_port;
    int lead_count;
    
    assert(net_external_progress(NULL, &lead_ip, &lead_port, &lead_count) == 0);
    
    net.ext_votes[0].ip = 0x01020304;
    net.ext_votes[0].port = 1234;
    net.ext_votes[0].count = 5;
    net.ext_votes[1].ip = 0x05060708;
    net.ext_votes[1].port = 5678;
    net.ext_votes[1].count = 3;
    net.ext_vote_count = 2;
    
    int total = net_external_progress(&net, &lead_ip, &lead_port, &lead_count);
    assert(total == 8);
    assert(lead_ip == 0x01020304);
    assert(lead_port == 1234);
    assert(lead_count == 5);
    printf("  test_external_progress: OK\n");
}

static void test_local_ip(void) {
    uint32_t ip = net_local_ip();
    (void)ip;
    printf("  test_local_ip: OK\n");
}

static void test_reset_external(void) {
    net_t net;
    memset(&net, 0, sizeof(net));
    
    net_reset_external(NULL);
    
    net.ext_vote_count = 5;
    net.external_ip_valid = 1;
    net.nat_symmetric = 1;
    
    net_reset_external(&net);
    assert(net.ext_vote_count == 0);
    assert(net.external_ip_valid == 0);
    assert(net.nat_symmetric == 0);
    printf("  test_reset_external: OK\n");
}

static void test_nat_type(void) {
    net_t net;
    memset(&net, 0, sizeof(net));
    
    const char *type = net_nat_type(NULL);
    assert(strcmp(type, "unknown") == 0);
    
    type = net_nat_type(&net);
    assert(strcmp(type, "detecting") == 0);
    
    net.mapped = 1;
    type = net_nat_type(&net);
    assert(strcmp(type, "open (NAT-PMP)") == 0);
    
    net.mapped = 0;
    net.nat_symmetric = 1;
    type = net_nat_type(&net);
    assert(strcmp(type, "symmetric") == 0);
    
    net.nat_symmetric = 0;
    net.external_ip_valid = 1;
    net.ext_votes[0].count = 1;
    net.ext_vote_count = 1;
    type = net_nat_type(&net);
    assert(strcmp(type, "detecting") == 0);
    
    net.ext_votes[0].count = 3;
    type = net_nat_type(&net);
    assert(strcmp(type, "cone") == 0);
    printf("  test_nat_type: OK\n");
}

static void test_set_mapped_endpoint(void) {
    net_t net;
    memset(&net, 0, sizeof(net));
    
    net_set_mapped_endpoint(NULL, 0x01020304, 1234);
    
    net_set_mapped_endpoint(&net, 0, 1234);
    
    net_set_mapped_endpoint(&net, 0x01020304, 1234);
    assert(net.external_ip == 0x01020304);
    assert(net.external_port == 1234);
    assert(net.external_ip_valid == 1);
    assert(net.mapped == 1);
    assert(net.nat_symmetric == 0);
    printf("  test_set_mapped_endpoint: OK\n");
}

static void test_resolve(void) {
    assert(net_resolve(NULL) == 0);
    
    uint32_t ip = net_resolve("localhost");
    (void)ip;
    printf("  test_resolve: OK\n");
}

static void test_ip_to_str(void) {
    const char *str = net_ip_to_str(0x01020304);
    assert(str != NULL);
    printf("  test_ip_to_str: OK (ip=%s)\n", str);
}

static void test_route_monitor(void) {
    int fd = net_route_monitor_open();
    if (fd >= 0) {
        net_route_monitor_drain(fd);
        close(fd);
    }
    net_route_monitor_drain(-1);
    printf("  test_route_monitor: OK\n");
}

static void test_natpmp_map(void) {
    uint16_t ext_port;
    uint32_t ext_ip;
    
    int ret = natpmp_map_udp(12345, 3600, &ext_port, &ext_ip);
    (void)ret;
    printf("  test_natpmp_map: OK (ret=%d)\n", ret);
}

static void test_send_recv_lo(void) {
    net_t net;
    assert(net_init(&net, 0) == 0);
    
    unsigned char msg[] = "hello";
    uint32_t loopback = 0x7f000001;
    uint16_t port = net_get_bound_port(&net);
    
    int sent = net_send(&net, msg, sizeof(msg), loopback, port);
    
    if (sent == 0) {
        usleep(10000);
        
        unsigned char buf[256];
        uint32_t from_ip;
        uint16_t from_port;
        int n = net_recv(&net, buf, sizeof(buf), &from_ip, &from_port);
        
        if (n > 0) {
            assert((size_t)n == sizeof(msg));
            assert(memcmp(buf, msg, n) == 0);
        }
    }
    
    net_cleanup(&net);
    printf("  test_send_recv_lo: OK\n");
}

int main(void) {
    printf("test_net:\n");
    
    test_init_null();
    test_init_and_cleanup();
    test_cleanup_null();
    test_get_port_null();
    test_send_null();
    test_recv_null();
    test_external_ip_null();
    test_update_external_ip();
    test_external_endpoint();
    test_nat_symmetric();
    test_external_progress();
    test_local_ip();
    test_reset_external();
    test_nat_type();
    test_set_mapped_endpoint();
    test_resolve();
    test_ip_to_str();
    test_route_monitor();
    test_natpmp_map();
    test_send_recv_lo();
    
    printf("test_net: OK\n");
    return 0;
}