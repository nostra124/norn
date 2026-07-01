/* SPDX-License-Identifier: MIT */
#ifndef MAINLINE_H
#define MAINLINE_H

#include <stdint.h>
#include <time.h>
#include "net.h"
#include "bencode.h"
#include "crypto.h"

#define MAINLINE_BOOTSTRAP_COUNT 3
#define MAINLINE_BOOTSTRAP_INTERVAL 300
#define MAINLINE_MAX_TRANSACTIONS 256
#define MAINLINE_TRANSACTION_TIMEOUT 30
#define MAINLINE_MAX_NODES 1024
#define MAINLINE_MAX_PER_SUBNET 8   /* routing-table entries allowed per /24 (BUG-014) */
#define MAINLINE_SERVE_HASHES 64    /* info_hashes we store peers for (as a DHT server) */
#define MAINLINE_SERVE_PEERS 16     /* peers kept per info_hash */

typedef struct {
    unsigned char id[20];
    uint32_t ip;
    uint16_t port;
    time_t last_seen;
    char pv[8];    /* peer's norn (protocol) version, major.minor (e.g. "0.12"); "" if unknown */
    char app[24];  /* peer's application name, from BEP-5 "v" (e.g. "norn-node", "Transmission"); "" if unknown */
    int is_preferred;  /* 1 if this peer runs norn (pv set) or our own application (app==self_app) */
    unsigned char pubkey[32];  /* peer's Ed25519 pubkey (norn "pk" extension); zeroed if unknown */
    int have_pubkey;    /* 1 if pubkey is valid (the peer speaks norn and sent pk) */
} mainline_node_t;

typedef struct mainline_transaction_t {
    uint32_t id;
    time_t created;
    int is_put;
    int is_mutable;
    unsigned char target[32];
    void *callback;
    void *user_data;
} mainline_transaction_t;

typedef struct {
    net_t *net;
    unsigned char self_id[20];
    unsigned char self_pub[32];     /* our ed25519 identity, published with our announce */
    int have_self_pub;
    unsigned char self_account[20]; /* hash(our account); we serve it authoritatively */
    int have_self_account;
    char self_version[24];          /* our norn version, reported in ping replies ("nv") */
    int have_self_version;
    char self_pv[8];                /* our protocol (norn) version, major.minor; reported as "pv" */
    char self_app[24];              /* our application name (e.g. "norn-node"); reported via BEP-5 "v" */
    int have_self_pv;

    mainline_node_t nodes[MAINLINE_MAX_NODES];
    int node_count;
    
    mainline_transaction_t transactions[MAINLINE_MAX_TRANSACTIONS];
    int transaction_count;
    uint32_t next_transaction_id;
    
    time_t last_bootstrap;
    
    char *bootstrap_hosts[MAINLINE_BOOTSTRAP_COUNT];
    uint16_t bootstrap_ports[MAINLINE_BOOTSTRAP_COUNT];

    /* Extra bootstrap peers (your own nodes) + private-overlay mode. */
    uint32_t boot_ips[8];
    uint16_t boot_ports[8];
    int boot_count;
    int private_mode;   /* if set, bootstrap ONLY to boot_* (a private DHT of your nodes) */
    int read_only;      /* BEP-43: tag queries with "ro"; don't answer queries; don't be added to tables */

    /* DHT-server state: peers announced to us, served on get_peers. */
    unsigned char token_secret[16];
    struct {
        unsigned char info_hash[20];
        struct { uint32_t ip; uint16_t port; } peers[MAINLINE_SERVE_PEERS];
        int peer_count;
        unsigned char pubkey[32];   /* announcer's ed25519 pubkey (0 if none) */
        int have_pubkey;
    } served[MAINLINE_SERVE_HASHES];
    int served_count;
} mainline_state_t;

typedef void (*mainline_callback_t)(void *user_data, const unsigned char *value, size_t value_len);

typedef void (*mainline_log_func_t)(const char *fmt, ...);

/* BUG-097: route mainline's own log lines through the daemon's logger (timestamp +
 * `mainline:` category) instead of a bare fprintf. NULL (default) = silent. */
void mainline_set_logger(mainline_log_func_t logf);

int mainline_init(mainline_state_t *state, net_t *net, const unsigned char *self_key);
void mainline_cleanup(mainline_state_t *state);

int mainline_add_node(mainline_state_t *state, const unsigned char *id, uint32_t ip, uint16_t port);
int mainline_get_node_count(mainline_state_t *state);

/* Update the version/application/pubkey metadata on a known routing-table node.
 * Called after mainline_add_node() when peer info (from a ping/find_node reply,
 * or the BEP-5 "v"/norn "pk"/"pv" extensions) is available. Empty strings / NULL
 * are ignored so a refresh doesn't clobber previously-learned values. Returns 0
 * if the node was found, -1 otherwise. */
int mainline_update_node_info(mainline_state_t *state, const unsigned char *id,
                                const char *pv, const char *app,
                                const unsigned char *pubkey);

/* Drop routing-table nodes not heard from in `max_age_secs` (BEP-5 marks a node
 * "questionable" after ~15 min). Keeps the table fresh and bounded; the periodic
 * bootstrap refills it when it falls below a healthy size. Returns the count
 * evicted. */
int mainline_evict_stale(mainline_state_t *state, long max_age_secs);

/* Persist / restore the routing table (warm restart). save returns the number
 * of nodes written (or -1); load returns the number restored (or -1). */
int mainline_save_nodes(mainline_state_t *state, const char *path);
int mainline_load_nodes(mainline_state_t *state, const char *path);

/* Account -> endpoint cache. Keyed by the 20-byte info_hash = hash(account).
 * Lets resolve/ping reuse a known endpoint and fall back to the (flaky) DHT
 * get_peers only when the cache misses or the cached endpoint is stale. */
#define PEER_CACHE_MAX 256
typedef struct {
    unsigned char key[20];
    uint32_t ip;
    uint16_t port;
    time_t updated;
} peer_cache_entry_t;
typedef struct {
    peer_cache_entry_t entries[PEER_CACHE_MAX];
    int count;
} peer_cache_t;

/* Look up key; if present and no older than max_age_sec (<=0 = any age), fill
 * ip/port and return 1, else 0. */
int peer_cache_get(peer_cache_t *c, const unsigned char *key,
                   uint32_t *ip, uint16_t *port, int max_age_sec);
/* Insert or refresh key -> ip:port (evicts the oldest entry when full). */
void peer_cache_put(peer_cache_t *c, const unsigned char *key, uint32_t ip, uint16_t port);
int peer_cache_save(peer_cache_t *c, const char *path);
int peer_cache_load(peer_cache_t *c, const char *path);
void mainline_crawl_network(mainline_state_t *state);
int mainline_bootstrap(mainline_state_t *state);
/* Register an extra bootstrap peer (one of your own nodes). */
void mainline_add_bootstrap(mainline_state_t *state, uint32_t ip, uint16_t port);
/* Private-overlay mode: bootstrap only to the registered peers, not the public routers. */
void mainline_set_private(mainline_state_t *state, int private_mode);
/* BEP-43 read-only mode (client-only/unreachable nodes): tag queries "ro", don't
 * answer queries, don't expect to be in others' routing tables. Skip announces. */
void mainline_set_read_only(mainline_state_t *state, int read_only);
int mainline_needs_bootstrap(mainline_state_t *state);
int mainline_find_node(mainline_state_t *state, const unsigned char *target, uint32_t ip, uint16_t port);

/* Synchronous iterative get_peers/announce_peer over the mainline DHT.
 * do_announce==0: search for a peer under info_hash, returns 1 and fills
 *   peer_ip/peer_port if found, else 0.
 * do_announce==1: advertise this node under info_hash with announce_port,
 *   returns the number of nodes announced to. Returns -1 on error. */
int mainline_lookup(mainline_state_t *state, const unsigned char *info_hash,
                    int do_announce, uint16_t announce_port,
                    uint32_t *peer_ip, uint16_t *peer_port, int timeout_ms,
                    mainline_log_func_t logf);

/* As mainline_lookup, but also fills peer_pub (32 bytes) with the account's
 * published ed25519 pubkey if available (NULL to ignore). */
int mainline_lookup_ex(mainline_state_t *state, const unsigned char *info_hash,
                       int do_announce, uint16_t announce_port,
                       uint32_t *peer_ip, uint16_t *peer_port,
                       unsigned char *peer_pub, int timeout_ms,
                       mainline_log_func_t logf);

/* Resolve an account by its node_id (= SHA256(account)[:20]) using an iterative
 * find_node — the node's contact comes from the DHT routing layer, with no
 * announce_peer/get_peers "values" and thus no public-DHT pollution. Prefers the
 * address the target itself answers from. Returns 1 and fills ip/port, else 0.
 * If confirmed_out is non-NULL it is set to 1 when the TARGET answered directly
 * (high confidence) and 0 when the result is only an unconfirmed routing contact
 * (BUG-101 — callers chasing a moved peer should require confirmed). */
int mainline_resolve_node(mainline_state_t *state, const unsigned char *node_id,
                          uint32_t *ip_out, uint16_t *port_out, int timeout_ms,
                          mainline_log_func_t logf, int *confirmed_out);

/* Iterative BEP-44 client (FEAT-022 stage 4c). do_put: publish {k,seq,sig,v} to
 * the closest token-bearing nodes (returns nodes put-to). do_put=0: fetch the
 * record at target=SHA1(k) — verified + stored via recstore_accept (returns 1 if
 * a record was accepted). target must be SHA1(k). */
int mainline_lookup_mutable(mainline_state_t *state, const unsigned char *target,
                            int do_put, const unsigned char *k, uint32_t seq,
                            const unsigned char *v, size_t vlen, const unsigned char *sig,
                            const unsigned char *salt, size_t saltlen, int immutable,
                            unsigned char *value_out, size_t *vlen_out, size_t vcap,
                            int timeout_ms, mainline_log_func_t logf);

int mainline_process_packet(mainline_state_t *state, const uint8_t *data, size_t len, uint32_t from_ip, uint16_t from_port);

void mainline_process_transactions(mainline_state_t *state);

int mainline_get_bootstrap_nodes(mainline_state_t *state, uint32_t *ips, uint16_t *ports, int max_count);

#endif