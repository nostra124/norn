/* SPDX-License-Identifier: MIT */
#include "peer_fetch.h"

#include "norn_session.h"
#include "served_proto.h"
#include "bep44.h"

#include <arpa/inet.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <sodium.h>

/* Per-request state for the async dial+fetch. */
typedef struct {
    norn_stream_t *stream;
    int sent;
    unsigned char rx[8192];
    size_t rxn;
    int have_status;
    int ok;
    uint64_t blen;
    size_t body_off;
    int done;
    int rc;
    const unsigned char *reqline;
    size_t reqlen;
    char errmsg[256];
} fetch_ctx_t;

static void on_stream(norn_stream_t *s, norn_stream_state_t st, void *ud) {
    fetch_ctx_t *c = ud;
    if (st == NORN_STREAM_READY && !c->sent) {
        norn_stream_write(s, c->reqline, c->reqlen);
        c->sent = 1;
    }
}

static void on_session(norn_session_t *session, norn_session_state_t st, void *ud) {
    fetch_ctx_t *c = ud;
    if (st == NORN_SESSION_ESTABLISHED && !c->stream) {
        c->stream = norn_stream_open_svc(session, NORN_SVC_SERVED_KV, on_stream, c);
        if (!c->stream) {
            c->done = 1;
            c->rc = -1;
        }
    } else if (st == NORN_SESSION_CLOSED) {
        c->done = 1;
    }
}

static void pump_rx(fetch_ctx_t *c) {
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
        if (i >= c->rxn) return;
        if (nornd_served_parse_status((const char *)c->rx, i + 1, &c->ok,
                                      &c->blen, NULL, 0) != 0) {
            snprintf(c->errmsg, sizeof(c->errmsg), "malformed response");
            c->done = 1;
            c->rc = -1;
            return;
        }
        c->have_status = 1;
        c->body_off = i + 1;
        if (!c->ok) {
            nornd_served_parse_status((const char *)c->rx, i + 1, &c->ok,
                                      &c->blen, c->errmsg, sizeof(c->errmsg));
            c->done = 1;
            c->rc = -1;
            return;
        }
    }
    if (c->have_status && c->ok) {
        size_t body = c->rxn - c->body_off;
        if (body >= c->blen) {
            c->done = 1;
            c->rc = 0;
        }
    }
}

int nornd_peer_fetch(norn_client_t *client,
                     const char *spec,
                     const char *verb,
                     const char *arg,
                     unsigned char *out, size_t *outlen,
                     char *err, size_t errcap) {
    if (!client || !spec || !verb || !out || !outlen) {
        if (err && errcap) snprintf(err, errcap, "invalid args");
        return -1;
    }

    /* Parse the peer spec: 40-hex node-id, 64-hex pubkey, or pubkey@host:port. */
    char specbuf[160];
    size_t sl = strlen(spec);
    if (sl >= sizeof(specbuf)) {
        if (err && errcap) snprintf(err, errcap, "peer spec too long");
        return -1;
    }
    memcpy(specbuf, spec, sl + 1);

    char *at = strchr(specbuf, '@');
    const char *host = NULL;
    uint16_t port = 0;
    if (at) {
        *at = '\0';
        char *colon = strrchr(at + 1, ':');
        if (!colon) {
            if (err && errcap) snprintf(err, errcap, "endpoint must be host:port");
            return -1;
        }
        *colon = '\0';
        host = at + 1;
        port = (uint16_t)atoi(colon + 1);
    }

    size_t pklen = strlen(specbuf);
    int is_node_id = 0;
    unsigned char peer_pub[32];
    unsigned char node_id[20];

    if (pklen == 40) {
        is_node_id = 1;
        if (sodium_hex2bin(node_id, sizeof(node_id), specbuf, pklen,
                           NULL, NULL, NULL) != 0) {
            if (err && errcap) snprintf(err, errcap, "bad 40-hex node-id");
            return -1;
        }
    } else if (pklen == 64) {
        if (sodium_hex2bin(peer_pub, sizeof(peer_pub), specbuf, pklen,
                           NULL, NULL, NULL) != 0) {
            if (err && errcap) snprintf(err, errcap, "bad 64-hex pubkey");
            return -1;
        }
    } else {
        if (err && errcap) snprintf(err, errcap,
                                    "spec must be 40 or 64 hex chars");
        return -1;
    }

    /* Parse the served-KV verb. */
    nornd_served_verb_t sv_verb;
    if (strcmp(verb, "get") == 0)
        sv_verb = NORND_SERVED_GET;
    else if (strcmp(verb, "cat") == 0)
        sv_verb = NORND_SERVED_CAT;
    else if (strcmp(verb, "list") == 0)
        sv_verb = NORND_SERVED_LIST;
    else if (strcmp(verb, "connect") == 0) {
        /* Just dial, no request needed. */
        sv_verb = NORND_SERVED_GET;
        arg = "";
    } else {
        if (err && errcap) snprintf(err, errcap,
                                    "unknown verb '%s'", verb);
        return -1;
    }

    /* Build the served request line. */
    char reqline[NORND_SERVED_MAX_ARG + 8];
    int rlen = nornd_served_encode_req(sv_verb, arg ? arg : "",
                                       reqline, sizeof(reqline));
    if (rlen < 0) {
        if (err && errcap) snprintf(err, errcap, "bad served-KV request");
        return -1;
    }

    /* Set up the async context. */
    fetch_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.rc = -1;
    ctx.reqline = (const unsigned char *)reqline;
    ctx.reqlen = (size_t)rlen;

    /* For "connect", we don't actually need to send/fetch — just dial. */
    int just_dial = (strcmp(verb, "connect") == 0);

    /* Dial. */
    int dialed;
    if (is_node_id) {
        /* 40-hex node-id: try routing table first, then full DHT resolve. */
        uint32_t lip = 0; uint16_t lport = 0;
        int have_local = norn_routing_lookup(client, node_id, &lip, &lport);
        if (have_local == 1) {
            norn_direct_endpoint_t ep;
            memset(&ep, 0, sizeof(ep));
            ep.ip = lip;
            ep.port = htons(lport);
            dialed = norn_dial_direct_async(client, &ep, NULL, NULL,
                                           on_session, &ctx);
        } else {
            uint32_t rip = 0; uint16_t rport = 0;
            unsigned char rpub[32];
            int found = norn_resolve_node(client, node_id, &rip, &rport,
                                          rpub, 10000);
            if (found != 1) {
                if (err && errcap)
                    snprintf(err, errcap,
                             "could not resolve node-id (not announced?)");
                return -1;
            }
            norn_direct_endpoint_t ep;
            memset(&ep, 0, sizeof(ep));
            ep.ip = rip;
            ep.port = htons(rport);
            dialed = norn_dial_direct_async(client, &ep, rpub, NULL,
                                           on_session, &ctx);
        }
    } else if (host) {
        /* Direct endpoint: parse IP and dial. */
        struct in_addr ipaddr;
        if (inet_pton(AF_INET, host, &ipaddr) != 1) {
            if (err && errcap) snprintf(err, errcap, "bad host '%s'", host);
            return -1;
        }
        norn_direct_endpoint_t ep;
        memset(&ep, 0, sizeof(ep));
        ep.ip = ipaddr.s_addr;
        ep.port = htons(port);
        dialed = norn_dial_direct_async(client, &ep, peer_pub, NULL,
                                       on_session, &ctx);
    } else {
        /* Resolve by pubkey via DHT. */
        dialed = norn_dial_async(client, peer_pub, NULL, on_session, &ctx);
    }

    if (dialed != 0) {
        if (err && errcap) snprintf(err, errcap, "dial failed");
        return -1;
    }

    /* If just dial (connect verb), we still pump briefly to establish. */
    int timeout_sec = just_dial ? 5 : 15;

    /* Pump the event loop until done or timeout. */
    int dht_fd = norn_get_fd(client);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    while (!ctx.done) {
        norn_tick(client);
        if (!just_dial) pump_rx(&ctx);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - t0.tv_sec) >= timeout_sec) {
            if (err && errcap)
                snprintf(err, errcap, "timed out (%ds)", timeout_sec);
            ctx.rc = -1;
            break;
        }

        /* Poll the DHT fd with a short timeout so we don't busy-loop. */
        if (dht_fd >= 0) {
            struct pollfd pfd = { .fd = dht_fd, .events = POLLIN, .revents = 0 };
            poll(&pfd, 1, 50);
        } else {
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 50000000 };
            nanosleep(&ts, NULL);
        }
    }

    if (ctx.rc != 0) {
        if (err && errcap && ctx.errmsg[0])
            snprintf(err, errcap, "%s", ctx.errmsg);
        return -1;
    }

    /* For "connect", success means the dial established. */
    if (just_dial) {
        *outlen = 0;
        return 0;
    }

    /* Copy the fetched body to the output buffer. */
    size_t body_len = (size_t)ctx.blen;
    size_t cap = *outlen;
    if (body_len > cap) body_len = cap;
    memcpy(out, ctx.rx + ctx.body_off, body_len);
    *outlen = body_len;
    return 0;
}
