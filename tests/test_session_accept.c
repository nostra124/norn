/* End-to-end loopback test for the async accept path (FEAT-029).
 *
 * Unlike test_session_loopback (which hand-wires fds and drives the low-level
 * handshake), this exercises the *public* async API over real loopback UDP:
 * one client norn_listen_async()es, another norn_dial_direct_async()es it, and
 * norn_tick() drives the full handshake through norn_client_tick_sessions() ——
 * including the listener demux that accepts a new peer and the shared-fd send
 * path. Then a stream is opened in each direction and bytes are verified across.
 *
 * Regression guard for the inbound-accept implementation: before it, the
 * listener silently dropped INIT packets and no session was ever established. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sodium.h>

#include "norn.h"
#include "norn_session.h"
#include "crypto.h"

static norn_stream_t *g_srv_inbound;   /* server's view of the client's stream */
static norn_session_t *g_srv_session;  /* accepted session on the server */
static norn_stream_t *g_cli_stream;    /* client's outbound stream */
static int g_cli_established;

static void on_srv_inbound_stream(norn_stream_t *s, void *ud) {
    (void)ud;
    g_srv_inbound = s;
}

static void on_accept(norn_session_t *s, void *ud) {
    (void)ud;
    g_srv_session = s;
    norn_session_set_accept_stream(s, on_srv_inbound_stream, NULL);
}

static void on_cli_stream(norn_stream_t *s, norn_stream_state_t st, void *ud) {
    (void)ud;
    if (st == NORN_STREAM_READY) g_cli_stream = s;
}

static void on_cli_session(norn_session_t *s, norn_session_state_t st, void *ud) {
    (void)ud;
    if (st == NORN_SESSION_ESTABLISHED) {
        g_cli_established = 1;
        norn_stream_open_async(s, on_cli_stream, NULL);
    }
}

static void pump(norn_client_t *a, norn_client_t *b, int rounds) {
    for (int i = 0; i < rounds; i++) {
        norn_tick(a);
        norn_tick(b);
        usleep(1000);
    }
}

int main(void) {
    assert(sodium_init() >= 0);

    keypair_t ks, kc;
    assert(crypto_keypair_new(&ks) == 0);
    assert(crypto_keypair_new(&kc) == 0);

    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test/accept";
    norn_client_t *srv = norn_new(ks.public_key, ks.secret_key, &cfg);
    norn_client_t *cli = norn_new(kc.public_key, kc.secret_key, &cfg);
    assert(srv && cli);

    /* Server listens; client dials it directly by its real pubkey. */
    uint16_t port = htons(45077);
    assert(norn_listen_async(srv, port, NULL, on_accept, NULL) == 0);

    norn_direct_endpoint_t ep;
    memset(&ep, 0, sizeof(ep));
    assert(inet_pton(AF_INET, "127.0.0.1", &ep.ip) == 1);
    ep.port = port;
    assert(norn_dial_direct_async(cli, &ep, ks.public_key, NULL,
                                  on_cli_session, NULL) == 0);

    /* Drive the handshake + client stream-open to completion. */
    for (int i = 0; i < 500 && !(g_cli_established && g_cli_stream); i++)
        pump(srv, cli, 1);

    assert(g_cli_established);          /* dialer reached ESTABLISHED */
    assert(g_srv_session != NULL);      /* listener accepted a new peer */
    assert(g_cli_stream != NULL);       /* client's stream is ready */

    /* Client -> server: the first segment surfaces the inbound stream server-side. */
    const char *c2s = "hello from the dialer";
    assert(norn_stream_write(g_cli_stream, (const unsigned char *)c2s,
                             strlen(c2s)) > 0);
    for (int i = 0; i < 200 && !(g_srv_inbound && norn_stream_readable(g_srv_inbound));
         i++)
        pump(srv, cli, 1);
    assert(g_srv_inbound != NULL);      /* server saw the peer-initiated stream */

    /* The accepted session authenticated the dialer as its real identity, not a
     * throwaway key (this is what makes peer addressing work for the cluster). */
    unsigned char seen[32];
    assert(norn_session_get_peer(g_srv_session, seen) == 0);
    assert(memcmp(seen, kc.public_key, 32) == 0);
    unsigned char got[128];
    int rn = norn_stream_read(g_srv_inbound, got, sizeof(got));
    assert(rn == (int)strlen(c2s) && memcmp(got, c2s, (size_t)rn) == 0);

    /* Server -> client over the same stream (exercises the shared-fd send back
     * to the dialer's connected socket). */
    const char *s2c = "and hello back from the responder";
    assert(norn_stream_write(g_srv_inbound, (const unsigned char *)s2c,
                             strlen(s2c)) > 0);
    for (int i = 0; i < 200 && norn_stream_readable(g_cli_stream) == 0; i++)
        pump(srv, cli, 1);
    rn = norn_stream_read(g_cli_stream, got, sizeof(got));
    assert(rn == (int)strlen(s2c) && memcmp(got, s2c, (size_t)rn) == 0);

    norn_free(srv);
    norn_free(cli);
    printf("test_session_accept: async accept + bidirectional stream ok\n");
    return 0;
}
