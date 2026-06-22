/* Test TCP transport implementation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "transport.h"
#include "transport_tcp.h"

/* Test invalid fd */
static void test_invalid_fd(void) {
    norn_transport_t *t = norn_tcp_new(-1, 0);
    assert(t == NULL);
}

/* Test basic create/close with should_close=0 */
static void test_create_close(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    
    norn_transport_t *t = norn_tcp_new(fd, 0);
    assert(t != NULL);
    assert(norn_transport_is_datagram(t) == 0);
    assert(norn_transport_is_stream(t) == 1);
    assert(norn_transport_modes(t) == NORN_STREAM);
    
    norn_transport_close(t);
    close(fd);
}

/* Test create with should_close=1 */
static void test_create_with_close(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    
    norn_transport_t *t = norn_tcp_new(fd, 1);
    assert(t != NULL);
    
    norn_transport_close(t);
    /* fd should be closed now */
}

/* Test dial (no-op for TCP, socket already connected) */
static void test_dial(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    
    norn_transport_t *t = norn_tcp_new(fd, 0);
    assert(t != NULL);
    
    int dial_rc = norn_transport_dial(t, NULL, 0);
    assert(dial_rc == 0);  /* TCP dial is no-op, returns success */
    
    norn_transport_close(t);
    close(fd);
}

/* Test cap (no special caps for TCP) */
static void test_cap(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    
    norn_transport_t *t = norn_tcp_new(fd, 0);
    assert(t != NULL);
    assert(norn_transport_cap(t) == 0);
    
    norn_transport_close(t);
    close(fd);
}

/* Test local endpoint */
static void test_local_endpoint(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  /* let bind choose port */
    
    int rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    assert(rc == 0);
    
    norn_transport_t *t = norn_tcp_new(fd, 0);
    assert(t != NULL);
    
    char buf[128];
    int len = norn_transport_local_endpoint(t, buf, sizeof(buf));
    assert(len > 0);  /* should return socket address length */
    assert(len >= (int)sizeof(struct sockaddr_in));
    
    norn_transport_close(t);
    close(fd);
}

/* Test NULL transport dispatchers */
static void test_null_dispatch(void) {
    norn_transport_close(NULL);  /* should not crash */
    assert(norn_transport_send(NULL, "test", 4) == -1);
    assert(norn_transport_recv(NULL, NULL, 0) == -1);
}

/* Test send/recv on socketpair (simplest way to test TCP send/recv without hanging) */
static void test_send_recv(void) {
    int fds[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(rc == 0);
    
    norn_transport_t *t = norn_tcp_new(fds[0], 1);
    assert(t != NULL);
    
    /* Send from transport, recv from other end */
    const char *msg = "hello";
    int sent = norn_transport_send(t, msg, 5);
    assert(sent == 5);
    
    char buf[64];
    int n = recv(fds[1], buf, sizeof(buf), 0);
    assert(n == 5);
    assert(memcmp(buf, "hello", 5) == 0);
    
    /* Send from other end, recv via transport */
    n = send(fds[1], "world", 5, 0);
    assert(n == 5);
    
    n = norn_transport_recv(t, buf, sizeof(buf));
    assert(n == 5);
    assert(memcmp(buf, "world", 5) == 0);
    
    norn_transport_close(t);
    close(fds[1]);
}

int main(void) {
    test_invalid_fd();
    test_create_close();
    test_create_with_close();
    test_dial();
    test_cap();
    test_local_endpoint();
    test_null_dispatch();
    test_send_recv();
    printf("transport_tcp tests passed\n");
    return 0;
}