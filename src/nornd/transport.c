/**
 * @file transport.c
 * @brief Multi-node cluster frame transport over norn sessions. See transport.h.
 *
 * One outbound session+stream per configured peer (we dial them); inbound peer
 * sessions are accepted and their streams drained into the cluster. Raft frames
 * are length-prefixed (peers.c) on the wire. Lost frames are fine — Raft resends
 * on its next heartbeat/append — so a peer with no ready stream simply drops.
 */

#include "transport.h"
#include "norn_raft.h" /* RAFT_MAX_NODES */

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INBOUND  (RAFT_MAX_NODES * 2)
#define REDIAL_TICKS 200 /* ~loop iterations between re-dial attempts */

typedef struct {
    nornd_transport_t *t;
    nornd_peer_t spec;
    norn_session_t *session;
    norn_stream_t *stream; /* outbound: write frames here when non-NULL */
    int established;
    int redial; /* countdown; 0 → (re)dial */
} out_peer_t;

typedef struct {
    int active;
    norn_session_t *session;
    unsigned char peer[NORN_CLUSTER_PUBKEY];
    norn_stream_t *stream;
    nornd_framer_t framer;
} in_conn_t;

struct nornd_transport {
    norn_client_t *client;
    norn_cluster_t *cluster;
    uint16_t listen_port;
    out_peer_t peers[RAFT_MAX_NODES];
    int n_peers;
    in_conn_t inbound[MAX_INBOUND];
};

/* ---- outbound ---- */

static void dial_peer(out_peer_t *op);

static void on_out_stream(norn_stream_t *stream, norn_stream_state_t state,
                          void *ud) {
    out_peer_t *op = ud;
    if (state == NORN_STREAM_READY)
        op->stream = stream;
    else { /* closed/reset → forget it, poll() will re-dial */
        op->stream = NULL;
    }
}

static void on_out_session(norn_session_t *session, norn_session_state_t state,
                           void *ud) {
    out_peer_t *op = ud;
    op->session = session;
    if (state == NORN_SESSION_ESTABLISHED) {
        op->established = 1;
        if (!op->stream)
            norn_stream_open_async(session, on_out_stream, op);
    } else if (state == NORN_SESSION_CLOSING || state == NORN_SESSION_CLOSED) {
        op->established = 0;
        op->session = NULL;
        op->stream = NULL;
        op->redial = REDIAL_TICKS;
    }
}

static void dial_peer(out_peer_t *op) {
    op->established = 0;
    op->stream = NULL;
    op->session = NULL;
    if (op->spec.direct) {
        norn_direct_endpoint_t ep;
        memset(&ep, 0, sizeof(ep));
        if (inet_pton(AF_INET, op->spec.host, &ep.ip) != 1) {
            op->redial = REDIAL_TICKS; /* unresolved host — try again later */
            return;
        }
        ep.port = htons(op->spec.port);
        norn_dial_direct_async(op->t->client, &ep, op->spec.pubkey, NULL,
                               on_out_session, op);
    } else {
        norn_dial_async(op->t->client, op->spec.pubkey, NULL, on_out_session, op);
    }
}

/* ---- inbound ---- */

static void on_inbound_stream(norn_stream_t *stream, void *ud) {
    in_conn_t *ic = ud;
    ic->stream = stream;
    nornd_framer_reset(&ic->framer);
    /* The session is ESTABLISHED by the time a stream opens, so the peer's
     * static pubkey (learned in the handshake) is now available — this is the
     * cluster member id we attribute inbound frames to. */
    norn_session_get_peer(ic->session, ic->peer);
}

static void on_accept(norn_session_t *session, void *ud) {
    nornd_transport_t *t = ud;
    in_conn_t *ic = NULL;
    for (int i = 0; i < MAX_INBOUND; i++) {
        if (!t->inbound[i].active) {
            ic = &t->inbound[i];
            break;
        }
    }
    if (!ic) return; /* table full — drop */
    memset(ic, 0, sizeof(*ic));
    ic->active = 1;
    ic->session = session;
    norn_session_set_accept_stream(session, on_inbound_stream, ic);
}

/* ---- lifecycle ---- */

nornd_transport_t *nornd_transport_new(norn_client_t *client,
                                       uint16_t listen_port,
                                       const nornd_peer_t *peers, int n_peers) {
    if (!client || n_peers < 0 || n_peers > RAFT_MAX_NODES ||
        (n_peers > 0 && !peers))
        return NULL;
    nornd_transport_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->client = client;
    t->listen_port = listen_port;
    t->n_peers = n_peers;
    for (int i = 0; i < n_peers; i++) {
        t->peers[i].t = t;
        t->peers[i].spec = peers[i];
        dial_peer(&t->peers[i]);
    }
    norn_listen_async(client, htons(listen_port), NULL, on_accept, t);
    return t;
}

void nornd_transport_set_cluster(nornd_transport_t *t, norn_cluster_t *cl) {
    if (t) t->cluster = cl;
}

void nornd_transport_send(void *ctx, const unsigned char pubkey[NORN_CLUSTER_PUBKEY],
                          const unsigned char *data, size_t len) {
    nornd_transport_t *t = ctx;
    if (!t || !data) return;
    for (int i = 0; i < t->n_peers; i++) {
        out_peer_t *op = &t->peers[i];
        if (!op->stream) continue;
        if (memcmp(op->spec.pubkey, pubkey, NORN_CLUSTER_PUBKEY) != 0) continue;
        unsigned char frame[NORND_FRAMER_CAP];
        int flen = nornd_frame_encode(data, len, frame, sizeof(frame));
        if (flen > 0) norn_stream_write(op->stream, frame, (size_t)flen);
        return;
    }
}

void nornd_transport_poll(nornd_transport_t *t) {
    if (!t) return;

    /* Drain inbound streams into the cluster. */
    unsigned char buf[NORND_FRAME_MAX];
    for (int i = 0; i < MAX_INBOUND; i++) {
        in_conn_t *ic = &t->inbound[i];
        if (!ic->active || !ic->stream) continue;
        while (norn_stream_readable(ic->stream) > 0) {
            int n = norn_stream_read(ic->stream, buf, sizeof(buf));
            if (n <= 0) break;
            if (nornd_framer_push(&ic->framer, buf, (size_t)n) != 0) break;
            const unsigned char *payload;
            size_t plen;
            int r;
            while ((r = nornd_framer_next(&ic->framer, &payload, &plen)) == 1) {
                if (t->cluster)
                    norn_cluster_input(t->cluster, ic->peer, payload, plen);
            }
            if (r < 0) { /* protocol violation — reset this connection */
                ic->active = 0;
                ic->stream = NULL;
                break;
            }
        }
    }

    /* Re-dial peers whose session has dropped. */
    for (int i = 0; i < t->n_peers; i++) {
        out_peer_t *op = &t->peers[i];
        if (op->session || op->stream) continue;
        if (op->redial > 0) {
            op->redial--;
            continue;
        }
        dial_peer(op);
    }
}

void nornd_transport_free(nornd_transport_t *t) { free(t); }
