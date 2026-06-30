/* SPDX-License-Identifier: MIT */
/* Integration test for the node-served-KV dial transport (FEAT-033): a peer
 * dials a node, opens a NORN_SVC_SERVED_KV stream, and CAT-fetches a file-backed
 * object — proving served-KV rides the app-mux over one session/port. Loopback
 * UDP, no external network. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sodium.h>

#include "norn_session.h"
#include "norn_session_internal.h"
#include "crypto.h"
#include "served.h"
#include "served_conn.h"
#include "store.h"

static int mk_sock(uint16_t *port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0);
    socklen_t l = sizeof(a);
    assert(getsockname(fd, (struct sockaddr *)&a, &l) == 0);
    *port = a.sin_port;
    return fd;
}
static void connect_to(int fd, uint16_t port) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = port;
    assert(connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0);
}

/* Server backend: GET/LIST stubs (unused here) + a real object store for CAT. */
static int fk_get(void *c, const unsigned char *k, size_t kl, unsigned char *o,
                  size_t cap) {
    (void)c; (void)k; (void)kl; (void)o; (void)cap;
    return -1;
}
static int fk_list(void *c, const unsigned char *p, size_t pl,
                   norn_kv_visit_fn fn, void *ud) {
    (void)c; (void)p; (void)pl; (void)fn; (void)ud;
    return 0;
}

/* The server records the inbound served-KV stream here. */
static norn_stream_t *g_serve_stream;
static void on_served_stream(norn_stream_t *s, void *ud) {
    (void)ud;
    g_serve_stream = s;
}
static void on_ready(norn_stream_t *s, norn_stream_state_t st, void *ud) {
    (void)s; (void)st; (void)ud;
}

static void drain(norn_session_t *s) {
    unsigned char buf[4096];
    ssize_t n;
    while ((n = recv(s->fd, buf, sizeof(buf), MSG_DONTWAIT)) > 0)
        norn_session_process_packet(s, buf, (size_t)n, 0, 0);
}

int main(void) {
    assert(sodium_init() >= 0);

    /* A file-backed object the server will CAT-serve. */
    char tmp[256];
    strcpy(tmp, "/tmp/norn_sdial_XXXXXX");
    assert(mkdtemp(tmp) != NULL);
    nornd_store_t store;
    assert(nornd_store_init(&store, tmp) == 0);
    unsigned char obj[1500]; /* > one chunk, to exercise streamed CAT */
    for (size_t i = 0; i < sizeof(obj); i++) obj[i] = (unsigned char)(i * 7 + 1);
    char hash[NORND_STORE_HASH_HEX + 1];
    assert(nornd_store_put(&store, obj, sizeof(obj), hash) == 0);

    nornd_served_backend_t be = {NULL, fk_get, fk_list, &store};

    keypair_t ka, kb;
    assert(crypto_keypair_new(&ka) == 0);
    assert(crypto_keypair_new(&kb) == 0);
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test/1.0";
    norn_client_t *client = norn_new(ka.public_key, ka.secret_key, &cfg);
    assert(client);
    /* Node hosts the served-KV service. */
    assert(norn_register_stream_service(client, NORN_SVC_SERVED_KV,
                                        on_served_stream, NULL) == 0);

    norn_session_t *a = norn_session_new(client, NULL); /* dialer */
    norn_session_t *b = norn_session_new(client, NULL); /* node    */
    assert(a && b);
    a->is_initiator = 1;
    b->is_initiator = 0;
    a->state = b->state = NORN_SESSION_CONNECTING;
    assert(norn_session_set_identity(a, ka.public_key, ka.secret_key) == 0);
    assert(norn_session_set_identity(b, kb.public_key, kb.secret_key) == 0);

    unsigned char init[512], resp[512], confirm[512];
    int il = norn_session_build_init(a, init, sizeof(init));
    int rl = norn_session_accept_init(b, init, (size_t)il, resp, sizeof(resp));
    int cl = norn_session_confirm_resp(a, resp, (size_t)rl, confirm, sizeof(confirm));
    a->state = NORN_SESSION_ESTABLISHED;
    assert(norn_session_finish_confirm(b, confirm, (size_t)cl) == 0);
    assert(b->state == NORN_SESSION_ESTABLISHED);

    uint16_t pa, pb;
    int sa = mk_sock(&pa), sb = mk_sock(&pb);
    connect_to(sa, pb);
    connect_to(sb, pa);
    a->fd = sa;
    b->fd = sb;

    /* Dialer opens a served-KV stream and sends "CAT <hash>". */
    norn_stream_t *req = norn_stream_open_svc(a, NORN_SVC_SERVED_KV, on_ready, NULL);
    assert(req);
    char line[80];
    int ln = snprintf(line, sizeof(line), "CAT %s\n", hash);
    assert(norn_stream_write(req, (const unsigned char *)line, (size_t)ln) > 0);

    /* Drive both ends: deliver the request to the node, run the serve driver,
     * deliver the response back to the dialer, and collect it. */
    nornd_serve_conn_t sc;
    int sc_init = 0, serve_done = 0;
    unsigned char rx[4096];
    size_t rxn = 0;
    for (int round = 0; round < 800; round++) {
        drain(a);
        drain(b);
        if (g_serve_stream && !sc_init) {
            nornd_serve_conn_init(&sc, g_serve_stream);
            sc_init = 1;
        }
        if (sc_init && !serve_done)
            serve_done = nornd_serve_conn_pump(&sc, &be);
        drain(a);
        drain(b);
        int n;
        unsigned char t[1024];
        while (rxn < sizeof(rx) &&
               (n = norn_stream_read(req, t, sizeof(t))) > 0) {
            size_t cp = (size_t)n;
            if (rxn + cp > sizeof(rx)) cp = sizeof(rx) - rxn;
            memcpy(rx + rxn, t, cp);
            rxn += cp;
        }
    }

    /* Parse the status line, then verify the streamed body equals the object. */
    size_t i = 0;
    while (i < rxn && rx[i] != '\n') i++;
    assert(i < rxn); /* found the status line */
    int ok = 0;
    uint64_t blen = 0;
    assert(nornd_served_parse_status((const char *)rx, i + 1, &ok, &blen, NULL, 0) == 0);
    assert(ok && blen == sizeof(obj));
    size_t body = rxn - (i + 1);
    assert(body == sizeof(obj));
    assert(memcmp(rx + i + 1, obj, sizeof(obj)) == 0);

    norn_session_free(a);
    norn_session_free(b);
    norn_free(client);
    close(sa);
    close(sb);
    printf("test_nornd_served_dial: CAT over dialed served-KV stream ok (%zu bytes)\n",
           sizeof(obj));
    return 0;
}
