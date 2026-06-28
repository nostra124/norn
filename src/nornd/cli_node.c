/**
 * @file cli_node.c
 * @brief `norn node …` peer content fetch (FEAT-033). See cli_node.h.
 *
 * A standalone async client: dial the peer, open one NORN_SVC_SERVED_KV stream,
 * send the request line, read the status line + body, print the body. Mirrors
 * the served-KV dial proven in tests/test_nornd_served_dial.c, over a real dial.
 */

#include "cli_node.h"

#include "crypto.h"
#include "norn.h"
#include "norn_session.h"
#include "served_proto.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sodium.h>

/* Response read state for the single in-flight request. */
typedef struct {
    norn_stream_t *stream;
    int sent;                       /* request written                       */
    unsigned char rx[8192];
    size_t rxn;
    int have_status;                /* status line parsed                    */
    int ok;
    uint64_t blen;                  /* advertised body length                */
    size_t body_off;                /* offset in rx where the body begins    */
    int done;
    int rc;
    const unsigned char *reqline;
    size_t reqlen;
} node_ctx_t;

static void on_stream(norn_stream_t *s, norn_stream_state_t st, void *ud) {
    node_ctx_t *c = ud;
    if (st == NORN_STREAM_READY && !c->sent) {
        norn_stream_write(s, c->reqline, c->reqlen);
        c->sent = 1;
    }
}

static void on_session(norn_session_t *session, norn_session_state_t st, void *ud) {
    node_ctx_t *c = ud;
    if (st == NORN_SESSION_ESTABLISHED && !c->stream) {
        c->stream = norn_stream_open_svc(session, NORN_SVC_SERVED_KV, on_stream, c);
        if (!c->stream) {
            c->done = 1;
            c->rc = 1;
        }
    } else if (st == NORN_SESSION_CLOSED) {
        c->done = 1; /* peer/path gone before we finished */
    }
}

/* Drain readable bytes, parse status once, then collect the body. */
static void pump_rx(node_ctx_t *c) {
    if (!c->stream) return;
    int n;
    unsigned char t[2048];
    while (c->rxn < sizeof(c->rx) &&
           (n = norn_stream_read(c->stream, t, sizeof(t))) > 0) {
        size_t cp = (size_t)n;
        if (c->rxn + cp > sizeof(c->rx)) cp = sizeof(c->rx) - c->rxn;
        memcpy(c->rx + c->rxn, t, cp);
        c->rxn += cp;
    }
    if (!c->have_status) {
        size_t i = 0;
        while (i < c->rxn && c->rx[i] != '\n') i++;
        if (i >= c->rxn) return; /* status line not complete yet */
        if (nornd_served_parse_status((const char *)c->rx, i + 1, &c->ok,
                                      &c->blen, NULL, 0) != 0) {
            fprintf(stderr, "norn node: malformed response\n");
            c->done = 1;
            c->rc = 1;
            return;
        }
        c->have_status = 1;
        c->body_off = i + 1;
        if (!c->ok) {
            /* Re-parse to surface the error message. */
            char err[256];
            nornd_served_parse_status((const char *)c->rx, i + 1, &c->ok, &c->blen,
                                      err, sizeof(err));
            fprintf(stderr, "norn node: %s\n", err);
            c->done = 1;
            c->rc = 1;
            return;
        }
    }
    if (c->have_status && c->ok) {
        size_t body = c->rxn - c->body_off;
        if (body >= c->blen) {
            fwrite(c->rx + c->body_off, 1, (size_t)c->blen, stdout);
            c->done = 1;
            c->rc = 0;
        }
    }
}

/* Load our CLI identity (raw keypair_t) from NORN_KEY or ~/.norn/key.pem. */
static int load_key(keypair_t *kp) {
    const char *path = getenv("NORN_KEY");
    char buf[600];
    if (!path) {
        const char *home = getenv("HOME");
        if (!home) return -1;
        snprintf(buf, sizeof(buf), "%s/.norn/key.pem", home);
        path = buf;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "norn node: cannot open key %s (run 'norn keygen')\n", path);
        return -1;
    }
    int ok = fread(kp, sizeof(*kp), 1, f) == 1;
    fclose(f);
    return ok ? 0 : -1;
}

int nornd_cli_node(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: norn node <pubkey[@host:port]> <get|cat|list> [arg]\n");
        return 2;
    }
    /* Parse <pubkey>[@host:port]. */
    char spec[160];
    if (strlen(argv[0]) >= sizeof(spec)) {
        fprintf(stderr, "norn node: peer spec too long\n");
        return 2;
    }
    strcpy(spec, argv[0]);
    char *at = strchr(spec, '@');
    const char *host = NULL;
    uint16_t port = 0;
    if (at) {
        *at = '\0';
        char *colon = strrchr(at + 1, ':');
        if (!colon) {
            fprintf(stderr, "norn node: endpoint must be host:port\n");
            return 2;
        }
        *colon = '\0';
        host = at + 1;
        port = (uint16_t)atoi(colon + 1);
    }
    unsigned char peer[32];
    if (sodium_hex2bin(peer, sizeof(peer), spec, strlen(spec), NULL, NULL, NULL) != 0) {
        fprintf(stderr, "norn node: bad 64-hex pubkey\n");
        return 2;
    }

    nornd_served_verb_t verb;
    if (strcmp(argv[1], "get") == 0) verb = NORND_SERVED_GET;
    else if (strcmp(argv[1], "cat") == 0) verb = NORND_SERVED_CAT;
    else if (strcmp(argv[1], "list") == 0) verb = NORND_SERVED_LIST;
    else {
        fprintf(stderr, "norn node: unknown verb '%s' (get|cat|list)\n", argv[1]);
        return 2;
    }
    const char *arg = argc >= 3 ? argv[2] : "";

    char reqline[NORND_SERVED_MAX_ARG + 8];
    int rlen = nornd_served_encode_req(verb, arg, reqline, sizeof(reqline));
    if (rlen < 0) {
        fprintf(stderr, "norn node: bad request (missing/oversized arg?)\n");
        return 2;
    }

    keypair_t kp;
    if (load_key(&kp) != 0) return 1;

    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "norn-node/0.12";
    norn_client_t *client = norn_new(kp.public_key, kp.secret_key, &cfg);
    if (!client) {
        fprintf(stderr, "norn node: failed to create client\n");
        return 1;
    }

    node_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.rc = 1;
    ctx.reqline = (const unsigned char *)reqline;
    ctx.reqlen = (size_t)rlen;

    int dialed;
    if (host) {
        norn_direct_endpoint_t ep;
        memset(&ep, 0, sizeof(ep));
        if (inet_pton(AF_INET, host, &ep.ip) != 1) {
            fprintf(stderr, "norn node: bad host %s\n", host);
            norn_free(client);
            return 2;
        }
        ep.port = htons(port);
        dialed = norn_dial_direct_async(client, &ep, peer, NULL, on_session, &ctx);
    } else {
        dialed = norn_dial_async(client, peer, NULL, on_session, &ctx);
    }
    if (dialed != 0) {
        fprintf(stderr, "norn node: dial failed\n");
        norn_free(client);
        return 1;
    }

    /* Pump until the response is complete or we time out (~10s). */
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (!ctx.done) {
        norn_tick(client);
        pump_rx(&ctx);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - t0.tv_sec) >= 10) {
            fprintf(stderr, "norn node: timed out\n");
            ctx.rc = 1;
            break;
        }
        usleep(2000);
    }

    norn_free(client);
    return ctx.rc;
}
