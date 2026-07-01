/* SPDX-License-Identifier: MIT */
/* Integration test for the private overlay (FEAT-020 / v0.10.0).
 *
 * Forms a private pubkey-mesh over loopback — no public mainline DHT — from a
 * single bootstrap node, and checks the two deterministic, observable halves of
 * acceptance #1:
 *
 *   (a) A fleet of >=3 nodes forms a private overlay from ONE bootstrap node and
 *       the nodes resolve each other by their (pubkey-derived) DHT ids.
 *   (b) The overlay never touches / is never polluted by the public DHT: every
 *       node's routing table contains ONLY fleet members. Public mainline routers
 *       are never contacted (mainline_bootstrap skips them under private_mode —
 *       norn.c) and so never enter the routing table.
 *
 * Acceptance #2 (a NAT'd member reachable via rendezvous/relay inside the
 * overlay) rides FEAT-017's relay path (FEAT-022, still open) and real network
 * topology, so it is a PIT concern, not a unit-level one.
 *
 * This drives the async mainline primitives directly (per-node routing tables,
 * unlike the process-global dhtstore) so several nodes can be pumped in one
 * process deterministically. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sodium.h>

#include "net.h"
#include "mainline.h"
#include "bep44.h"
#include "crypto.h"

#define FLEET 3

typedef struct {
    keypair_t kp;
    net_t net;
    mainline_state_t ml;
    uint16_t port; /* network byte order */
} node_t;

static void node_init(node_t *n) {
    assert(crypto_keypair_new(&n->kp) == 0);
    unsigned char id[20];
    bep44_target_for_pubkey(id, n->kp.public_key);
    assert(net_init(&n->net, 0) == 0);
    n->port = net_get_bound_port(&n->net);
    assert(mainline_init(&n->ml, &n->net, id) == 0);
    mainline_set_private(&n->ml, 1); /* never touch the public mainline DHT */
}

static void node_free(node_t *n) {
    mainline_cleanup(&n->ml);
    net_cleanup(&n->net);
}

/* Drain every node's socket into its mainline state and drive its timers. */
static void pump(node_t *nodes, int n, int rounds) {
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < n; i++) {
            uint8_t buf[2048];
            uint32_t fip;
            uint16_t fport;
            int len;
            while ((len = net_recv(&nodes[i].net, buf, sizeof buf, &fip, &fport)) > 0)
                mainline_process_packet(&nodes[i].ml, buf, (size_t)len, fip, fport);
            mainline_process_transactions(&nodes[i].ml);
        }
        usleep(700);
    }
}

/* (a) overlay forms from a single bootstrap node; (b) routing stays fleet-only. */
static void test_private_overlay_forms(void) {
    node_t nodes[FLEET];
    for (int i = 0; i < FLEET; i++) node_init(&nodes[i]);

    /* Every member (1..) bootstraps to node 0 ONLY — a single seed. */
    uint32_t lo = htonl(INADDR_LOOPBACK);
    for (int i = 1; i < FLEET; i++) {
        mainline_add_bootstrap(&nodes[i].ml, lo, nodes[0].port);
        assert(mainline_bootstrap(&nodes[i].ml) == 0);
    }
    pump(nodes, FLEET, 1000);

    int counts[FLEET];
    for (int i = 0; i < FLEET; i++) counts[i] = mainline_get_node_count(&nodes[i].ml);

    /* Seed resolved every member by its pubkey-derived id. */
    assert(counts[0] == FLEET - 1);
    /* Every member resolved at least the seed. */
    int cross = 0;
    for (int i = 1; i < FLEET; i++) {
        assert(counts[i] >= 1);
        if (counts[i] >= 2) cross = 1; /* learned a peer beyond the seed */
        /* Fleet-only: a node can know at most the other FLEET-1 nodes. A public
         * router replying would push this above FLEET-1. */
        assert(counts[i] <= FLEET - 1);
    }
    assert(counts[0] <= FLEET - 1);

    /* Members resolve EACH OTHER over the overlay, not just the seed: at least
     * one member learned a non-seed peer through the seed's referrals. */
    assert(cross == 1);

    for (int i = 0; i < FLEET; i++) node_free(&nodes[i]);
    printf("  test_private_overlay_forms: OK (seed knows %d, cross-link formed)\n",
           FLEET - 1);
}

/* A private node with NO fleet peers initiates nothing on bootstrap — it does
 * not fall back to the public routers (which would otherwise be contacted). */
static void test_private_no_peers_is_silent(void) {
    node_t a;
    node_init(&a);
    assert(mainline_bootstrap(&a.ml) == 0); /* zero boot peers, private */
    pump(&a, 1, 200);
    assert(mainline_get_node_count(&a.ml) == 0);
    node_free(&a);
    printf("  test_private_no_peers_is_silent: OK\n");
}

int main(void) {
    assert(sodium_init() >= 0);
    printf("test_overlay_net:\n");
    test_private_overlay_forms();
    test_private_no_peers_is_silent();
    printf("test_overlay_net: OK\n");
    return 0;
}
