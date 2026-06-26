/**
 * @file transport.h
 * @brief Multi-node cluster frame transport over norn sessions (FEAT-029).
 *
 * Wires the cluster's `io.send` to deliver opaque Raft frames to peers over
 * norn sessions, and routes inbound frames back into norn_cluster_input. Peers
 * are dialed by pubkey (DHT-resolved) or by a direct endpoint; each node also
 * listens for inbound peer sessions. Daemon glue — not unit-coverage-tracked;
 * the pure framing/parsing it relies on lives in peers.c.
 */
#ifndef NORND_TRANSPORT_H
#define NORND_TRANSPORT_H

#include <stdint.h>
#include "norn_session.h"
#include "norn_cluster.h"
#include "peers.h"

typedef struct nornd_transport nornd_transport_t;

/**
 * Create the transport: start listening on `listen_port` (host byte order; 0 =
 * auto) and dial each configured peer. `peers` is copied. Returns NULL on bad
 * args / allocation failure.
 */
nornd_transport_t *nornd_transport_new(norn_client_t *client,
                                       uint16_t listen_port,
                                       const nornd_peer_t *peers, int n_peers);

/** Attach the cluster that inbound frames are fed into. */
void nornd_transport_set_cluster(nornd_transport_t *t, norn_cluster_t *cl);

/** norn_cluster io.send hook: frame `data` and write it to `pubkey`'s stream
 *  (dropped if that peer isn't connected yet — Raft retransmits). `ctx` is the
 *  transport. */
void nornd_transport_send(void *ctx, const unsigned char pubkey[NORN_CLUSTER_PUBKEY],
                          const unsigned char *data, size_t len);

/** Drain inbound peer streams into the cluster and re-dial dropped peers. Call
 *  once per event-loop iteration after norn_tick. */
void nornd_transport_poll(nornd_transport_t *t);

void nornd_transport_free(nornd_transport_t *t);

#endif /* NORND_TRANSPORT_H */
