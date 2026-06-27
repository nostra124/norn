/* End-to-end loopback test for the session stream data path (FEAT-016/018).
 *
 * Establishes two real sessions over loopback UDP sockets, then verifies that a
 * stream opened on the initiator is surfaced to the responder via the inbound
 * stream-accept handler and that bytes flow across — the server side of a
 * stream tunnel. No external network. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sodium.h>

#include "norn_session.h"
#include "norn_session_internal.h"
#include "crypto.h"

/* A connected loopback UDP socket; returns fd and fills *port (network order). */
static int mk_sock(uint16_t *port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
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

/* External handshake signer (ssh-agent stand-in): signs with the raw key in ud.
 * Proves the session layer authenticates via norn_session_set_signer without the
 * secret living in session->self_secret. */
static int ext_signer(void *ud, unsigned char sig[64], const unsigned char *msg,
                      size_t msglen) {
    return bf_sign(sig, msg, msglen, (const unsigned char *)ud);
}

static norn_stream_t *g_accepted;
static void on_accept(norn_stream_t *s, void *ud) {
    (void)ud;
    g_accepted = s;
}

static void on_stream_ready(norn_stream_t *s, norn_stream_state_t st, void *ud) {
    (void)s;
    (void)st;
    (void)ud;
}

/* Pump both sessions: drain sockets into process_packet, drive mux timers. */
static void pump(norn_session_t *a, norn_session_t *b) {
    norn_session_t *ses[2] = {a, b};
    for (int round = 0; round < 200; round++) {
        for (int i = 0; i < 2; i++) {
            unsigned char buf[4096];
            ssize_t n;
            while ((n = recv(ses[i]->fd, buf, sizeof(buf), MSG_DONTWAIT)) > 0)
                norn_session_process_packet(ses[i], buf, (size_t)n, 0, 0);
        }
    }
}

int main(void) {
    assert(sodium_init() >= 0);

    keypair_t ka, kb;
    assert(crypto_keypair_new(&ka) == 0);
    assert(crypto_keypair_new(&kb) == 0);

    /* A client is required to allocate a session; its own socket is unused here
     * (we wire the session fds directly for the loopback data plane). */
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test/1.0";
    norn_client_t *client = norn_new(ka.public_key, ka.secret_key, &cfg);
    assert(client);

    norn_session_t *a = norn_session_new(client, NULL);
    norn_session_t *b = norn_session_new(client, NULL);
    assert(a && b);
    a->is_initiator = 1;
    b->is_initiator = 0;
    a->state = NORN_SESSION_CONNECTING;
    b->state = NORN_SESSION_CONNECTING;
    assert(norn_session_set_identity(a, ka.public_key, ka.secret_key) == 0);
    assert(norn_session_set_identity(b, kb.public_key, kb.secret_key) == 0);

    /* Low-level handshake (no sockets needed to derive keys). */
    unsigned char init[512], resp[512], confirm[512];
    int il = norn_session_build_init(a, init, sizeof(init));
    assert(il > 0);
    int rl = norn_session_accept_init(b, init, (size_t)il, resp, sizeof(resp));
    assert(rl > 0);
    int cl = norn_session_confirm_resp(a, resp, (size_t)rl, confirm, sizeof(confirm));
    assert(cl > 0);
    a->state = NORN_SESSION_ESTABLISHED; /* initiator established after CONFIRM */
    assert(norn_session_finish_confirm(b, confirm, (size_t)cl) == 0);
    assert(b->state == NORN_SESSION_ESTABLISHED);

    /* Wire connected loopback sockets for the data plane. */
    uint16_t pa, pb;
    int sa = mk_sock(&pa);
    int sb = mk_sock(&pb);
    connect_to(sa, pb);
    connect_to(sb, pa);
    a->fd = sa;
    b->fd = sb;

    /* Server side: accept inbound streams. */
    assert(norn_session_set_accept_stream(b, on_accept, NULL) == 0);

    /* Client side: open a stream and write through it. */
    norn_stream_t *stream = norn_stream_open_async(a, on_stream_ready, NULL);
    assert(stream);
    const char *msg = "hello world over a pubkey stream";
    assert(norn_stream_write(stream, (const unsigned char *)msg, strlen(msg)) > 0);

    /* Drive the data plane: the segment is sealed, sent, decrypted, and the
     * inbound stream surfaced to the responder, which reads the bytes. */
    pump(a, b);

    assert(g_accepted != NULL); /* server saw the peer-initiated stream */
    unsigned char got[128];
    int rn = norn_stream_read(g_accepted, got, sizeof(got));
    assert(rn == (int)strlen(msg));
    assert(memcmp(got, msg, (size_t)rn) == 0);

    /* FEAT-028: the same 3-message handshake authenticated through an external
     * signer (the secret never enters session->self_secret). Both ends install a
     * signer over a *placeholder* secret-in-self_secret and a real key via the
     * signer; the handshake must still establish and bind true identities. */
    {
        norn_session_t *c = norn_session_new(client, NULL);
        norn_session_t *d = norn_session_new(client, NULL);
        assert(c && d);
        c->is_initiator = 1;
        d->is_initiator = 0;
        c->state = NORN_SESSION_CONNECTING;
        d->state = NORN_SESSION_CONNECTING;
        /* self_secret left zeroed; only the public key + signer are real. */
        unsigned char zero[64];
        memset(zero, 0, sizeof(zero));
        assert(norn_session_set_identity(c, ka.public_key, zero) == 0);
        assert(norn_session_set_identity(d, kb.public_key, zero) == 0);
        norn_session_set_signer(c, ext_signer, ka.secret_key);
        norn_session_set_signer(d, ext_signer, kb.secret_key);

        unsigned char i2[512], r2[512], c2[512];
        int il2 = norn_session_build_init(c, i2, sizeof(i2));
        assert(il2 > 0);
        int rl2 = norn_session_accept_init(d, i2, (size_t)il2, r2, sizeof(r2));
        assert(rl2 > 0);
        int cl2 = norn_session_confirm_resp(c, r2, (size_t)rl2, c2, sizeof(c2));
        assert(cl2 > 0);
        assert(norn_session_finish_confirm(d, c2, (size_t)cl2) == 0);
        assert(d->state == NORN_SESSION_ESTABLISHED);
        /* each side authenticated the other's true identity via the signer */
        unsigned char seen[64];
        assert(norn_session_get_peer(d, seen) == 0);
        assert(memcmp(seen, ka.public_key, 32) == 0);
        norn_session_free(c);
        norn_session_free(d);
    }

    norn_session_free(a);
    norn_session_free(b);
    norn_free(client);
    close(sa);
    close(sb);
    printf("test_session_loopback: stream tunnel ok (%d bytes)\n", rn);
    return 0;
}
