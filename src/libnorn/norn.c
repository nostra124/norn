/* SPDX-License-Identifier: MIT */
#include "mainline.h"
#include "crypto.h"
#include "bep44.h"      /* BEP-44 mutable items: target = SHA1(k) */
#include "dhtstore.h"   /* bounded good-citizen store for served records */
#include "recstore.h"   /* fetched records funnel through the same validating gate */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sodium.h>     /* CSPRNG (randombytes) + keyed hash (crypto_generichash) */
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>

/* BUG-097: optional daemon logger so mainline's lines match the rest of the log
 * (`[timestamp] mainline: …`). Unset → these lines are silent. */
static mainline_log_func_t g_ml_log = NULL;
void mainline_set_logger(mainline_log_func_t logf) { g_ml_log = logf; }
#define ML_LOG(...) do { if (g_ml_log) g_ml_log(__VA_ARGS__); } while (0)

static const char *BOOTSTRAP_HOSTS[] = {
    "router.bittorrent.com",
    "router.utorrent.com",
    "dht.transmissionbt.com"
};
static const uint16_t BOOTSTRAP_PORTS[] = {6881, 6881, 6881};

static uint32_t mainline_random_id(void) {
    uint32_t id;
    randombytes_buf(&id, sizeof(id));   /* CSPRNG, not rand() — tids/secret (BUG-015) */
    return id;
}

int mainline_init(mainline_state_t *state, net_t *net, const unsigned char *self_key) {
    if (!state || !net || !self_key) return -1;
    
    memset(state, 0, sizeof(mainline_state_t));
    state->net = net;
    
    memcpy(state->self_id, self_key, 20);

    randombytes_buf(state->token_secret, 16);   /* CSPRNG secret (BUG-015) */

    state->next_transaction_id = mainline_random_id();
    state->transaction_count = 0;
    state->node_count = 0;
    
    for (int i = 0; i < MAINLINE_BOOTSTRAP_COUNT; i++) {
        state->bootstrap_hosts[i] = strdup(BOOTSTRAP_HOSTS[i]);
        state->bootstrap_ports[i] = BOOTSTRAP_PORTS[i];
    }
    
    state->last_bootstrap = 0;
    
    return 0;
}

void mainline_cleanup(mainline_state_t *state) {
    if (!state) return;
    
    for (int i = 0; i < MAINLINE_BOOTSTRAP_COUNT; i++) {
        free(state->bootstrap_hosts[i]);
        state->bootstrap_hosts[i] = NULL;
    }
}

int mainline_add_node(mainline_state_t *state, const unsigned char *id, uint32_t ip, uint16_t port) {
    if (!state || !id) return -1;
    if (state->node_count >= MAINLINE_MAX_NODES) return -1;
    
    if (ip == 0 || port == 0) return -1;
    
    if (memcmp(id, state->self_id, 20) == 0) return -1;
    
    for (int i = 0; i < state->node_count; i++) {
        if (memcmp(state->nodes[i].id, id, 20) == 0) {
            state->nodes[i].ip = ip;
            state->nodes[i].port = port;
            state->nodes[i].last_seen = time(NULL);
            return 0;
        }
    }

    /* Per-/24 admission cap (BUG-014): a peer can name arbitrary node ids in its
     * 'nodes' reply, so without a subnet limit one attacker-controlled /24 fills
     * the table and eclipses lookups. Cap how many table entries share a /24. */
    uint32_t new_net = ntohl(ip) & 0xffffff00u;
    int same_net = 0;
    for (int i = 0; i < state->node_count; i++)
        if ((ntohl(state->nodes[i].ip) & 0xffffff00u) == new_net) same_net++;
    if (same_net >= MAINLINE_MAX_PER_SUBNET) return -1;

    memcpy(state->nodes[state->node_count].id, id, 20);
    state->nodes[state->node_count].ip = ip;
    state->nodes[state->node_count].port = port;
    state->nodes[state->node_count].last_seen = time(NULL);
    state->node_count++;
    
    return 1;
}

int mainline_get_node_count(mainline_state_t *state) {
    if (!state) return 0;
    return state->node_count;
}

int mainline_evict_stale(mainline_state_t *state, long max_age_secs) {
    if (!state) return 0;
    time_t now = time(NULL);
    int kept = 0, evicted = 0;
    for (int i = 0; i < state->node_count; i++) {
        if ((long)(now - state->nodes[i].last_seen) > max_age_secs) { evicted++; continue; }
        if (kept != i) state->nodes[kept] = state->nodes[i];
        kept++;
    }
    state->node_count = kept;
    return evicted;
}

/* Persist the routing table so the daemon restarts with a warm table instead
 * of rebuilding from bootstrap. Simple per-host binary format. */
#define MAINLINE_NODES_MAGIC 0x4e544844u  /* "DHTN" */
#define MAINLINE_NODES_VERSION 1u

int mainline_save_nodes(mainline_state_t *state, const char *path) {
    if (!state || !path) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t magic = MAINLINE_NODES_MAGIC, ver = MAINLINE_NODES_VERSION;
    uint32_t count = (uint32_t)state->node_count;
    int ok = (fwrite(&magic, 4, 1, f) == 1) &&
             (fwrite(&ver, 4, 1, f) == 1) &&
             (fwrite(&count, 4, 1, f) == 1);
    for (int i = 0; ok && i < state->node_count; i++) {
        ok = (fwrite(state->nodes[i].id, 1, 20, f) == 20) &&
             (fwrite(&state->nodes[i].ip, 4, 1, f) == 1) &&
             (fwrite(&state->nodes[i].port, 2, 1, f) == 1);
    }
    fclose(f);
    return ok ? (int)count : -1;
}

int peer_cache_get(peer_cache_t *c, const unsigned char *key,
                   uint32_t *ip, uint16_t *port, int max_age_sec) {
    if (!c || !key) return 0;
    time_t now = time(NULL);
    for (int i = 0; i < c->count; i++) {
        if (memcmp(c->entries[i].key, key, 20) == 0) {
            if (max_age_sec > 0 && (now - c->entries[i].updated) > max_age_sec) return 0;
            if (ip) *ip = c->entries[i].ip;
            if (port) *port = c->entries[i].port;
            return 1;
        }
    }
    return 0;
}

void peer_cache_put(peer_cache_t *c, const unsigned char *key, uint32_t ip, uint16_t port) {
    if (!c || !key || ip == 0 || port == 0) return;
    time_t now = time(NULL);
    for (int i = 0; i < c->count; i++) {
        if (memcmp(c->entries[i].key, key, 20) == 0) {
            c->entries[i].ip = ip;
            c->entries[i].port = port;
            c->entries[i].updated = now;
            return;
        }
    }
    int slot;
    if (c->count < PEER_CACHE_MAX) {
        slot = c->count++;
    } else {
        slot = 0;  /* evict the oldest */
        for (int i = 1; i < c->count; i++)
            if (c->entries[i].updated < c->entries[slot].updated) slot = i;
    }
    memcpy(c->entries[slot].key, key, 20);
    c->entries[slot].ip = ip;
    c->entries[slot].port = port;
    c->entries[slot].updated = now;
}

#define PEER_CACHE_MAGIC 0x43504844u  /* "DHPC" */
#define PEER_CACHE_VERSION 1u

int peer_cache_save(peer_cache_t *c, const char *path) {
    if (!c || !path) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t magic = PEER_CACHE_MAGIC, ver = PEER_CACHE_VERSION, count = (uint32_t)c->count;
    int ok = (fwrite(&magic, 4, 1, f) == 1) && (fwrite(&ver, 4, 1, f) == 1) &&
             (fwrite(&count, 4, 1, f) == 1);
    for (int i = 0; ok && i < c->count; i++) {
        int64_t ts = (int64_t)c->entries[i].updated;
        ok = (fwrite(c->entries[i].key, 1, 20, f) == 20) &&
             (fwrite(&c->entries[i].ip, 4, 1, f) == 1) &&
             (fwrite(&c->entries[i].port, 2, 1, f) == 1) &&
             (fwrite(&ts, 8, 1, f) == 1);
    }
    fclose(f);
    return ok ? (int)count : -1;
}

int peer_cache_load(peer_cache_t *c, const char *path) {
    if (!c || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t magic = 0, ver = 0, count = 0;
    if (fread(&magic, 4, 1, f) != 1 || fread(&ver, 4, 1, f) != 1 ||
        fread(&count, 4, 1, f) != 1 ||
        magic != PEER_CACHE_MAGIC || ver != PEER_CACHE_VERSION) {
        fclose(f);
        return -1;
    }
    int loaded = 0;
    for (uint32_t i = 0; i < count && c->count < PEER_CACHE_MAX; i++) {
        peer_cache_entry_t e;
        int64_t ts = 0;
        if (fread(e.key, 1, 20, f) != 20 || fread(&e.ip, 4, 1, f) != 1 ||
            fread(&e.port, 2, 1, f) != 1 || fread(&ts, 8, 1, f) != 1) {
            break;
        }
        e.updated = (time_t)ts;
        c->entries[c->count++] = e;
        loaded++;
    }
    fclose(f);
    return loaded;
}

int mainline_load_nodes(mainline_state_t *state, const char *path) {
    if (!state || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t magic = 0, ver = 0, count = 0;
    if (fread(&magic, 4, 1, f) != 1 || fread(&ver, 4, 1, f) != 1 ||
        fread(&count, 4, 1, f) != 1 ||
        magic != MAINLINE_NODES_MAGIC || ver != MAINLINE_NODES_VERSION) {
        fclose(f);
        return -1;
    }
    int loaded = 0;
    for (uint32_t i = 0; i < count && state->node_count < MAINLINE_MAX_NODES; i++) {
        unsigned char id[20];
        uint32_t ip;
        uint16_t port;
        if (fread(id, 1, 20, f) != 20 || fread(&ip, 4, 1, f) != 1 ||
            fread(&port, 2, 1, f) != 1) {
            break;
        }
        if (mainline_add_node(state, id, ip, port) >= 0) loaded++;
    }
    fclose(f);
    return loaded;
}

void mainline_crawl_network(mainline_state_t *state) {
    if (!state) return;
    
    int queries_sent = 0;
    int max_queries = 10;
    char targets[600]; size_t to = 0; targets[0] = '\0';   /* BUG-100: name the targets */

    for (int i = 0; i < state->node_count && queries_sent < max_queries; i++) {
        if (state->nodes[i].ip != 0 && state->nodes[i].port != 0) {
            mainline_find_node(state, state->self_id, state->nodes[i].ip, state->nodes[i].port);
            if (to < sizeof(targets) - 32)
                to += snprintf(targets + to, sizeof(targets) - to, "%s%s:%u",
                               to ? ", " : "", net_ip_to_str(state->nodes[i].ip),
                               state->nodes[i].port);
            queries_sent++;
        }
    }

    if (queries_sent > 0)
        ML_LOG("mainline: crawl — find_node to %d node(s): %s", queries_sent, targets);
}

static char *encode_transaction_id(uint32_t tid) {
    char *str = malloc(5);
    if (!str) return NULL;
    str[0] = (tid >> 24) & 0xFF;
    str[1] = (tid >> 16) & 0xFF;
    str[2] = (tid >> 8) & 0xFF;
    str[3] = tid & 0xFF;
    str[4] = '\0';
    return str;
}

static uint32_t decode_transaction_id(const char *tid, size_t len) {
    if (!tid || len < 4) return 0;
    return ((uint32_t)(uint8_t)tid[0] << 24) | 
           ((uint32_t)(uint8_t)tid[1] << 16) | 
           ((uint32_t)(uint8_t)tid[2] << 8) | 
           (uint32_t)(uint8_t)tid[3];
}

void mainline_add_bootstrap(mainline_state_t *state, uint32_t ip, uint16_t port) {
    if (!state || ip == 0 || port == 0) return;
    if (state->boot_count >= (int)(sizeof(state->boot_ips) / sizeof(state->boot_ips[0]))) return;
    state->boot_ips[state->boot_count] = ip;
    state->boot_ports[state->boot_count] = port;
    state->boot_count++;
}

void mainline_set_private(mainline_state_t *state, int private_mode) {
    if (state) state->private_mode = private_mode ? 1 : 0;
}

void mainline_set_read_only(mainline_state_t *state, int read_only) {
    if (state) state->read_only = read_only ? 1 : 0;
}

/* BEP-43: a read-only node adds top-level "ro":1 to every query it sends, so
 * well-behaved peers don't insert it into their routing tables. No-op otherwise. */
static void mainline_tag_ro(mainline_state_t *state, bencode_value_t *dict) {
    if (state && state->read_only)
        bencode_dict_add(dict, "ro", bencode_int_new(1));
}

int mainline_bootstrap(mainline_state_t *state) {
    if (!state) return -1;

    /* Local bootstrap: try the system nornd on 127.0.0.1:6881 first so
     * user-mode instances discover the local daemon immediately. Harmless
     * self-query when the daemon itself is on that port. */
    {
        uint32_t local_ip = htonl(0x7F000001); /* 127.0.0.1 */
        mainline_find_node(state, state->self_id, local_ip, 6881);
    }

    /* Your own nodes (private overlay seeds). */
    for (int i = 0; i < state->boot_count; i++) {
        mainline_find_node(state, state->self_id, state->boot_ips[i], state->boot_ports[i]);
    }
    /* Public mainline routers, unless running a private overlay. */
    if (!state->private_mode) {
        for (int i = 0; i < MAINLINE_BOOTSTRAP_COUNT; i++) {
            uint32_t ip = net_resolve(state->bootstrap_hosts[i]);
            if (ip == 0) continue;
            mainline_find_node(state, state->self_id, ip, state->bootstrap_ports[i]);
        }
    }

    state->last_bootstrap = time(NULL);
    return 0;
}

int mainline_needs_bootstrap(mainline_state_t *state) {
    if (!state) return 0;
    
    time_t now = time(NULL);
    return ((now - state->last_bootstrap) >= MAINLINE_BOOTSTRAP_INTERVAL);
}

int mainline_find_node(mainline_state_t *state, const unsigned char *target, uint32_t ip, uint16_t port) {
    if (!state || !target) return -1;
    
    bencode_value_t *dict = bencode_dict_new();
    if (!dict) return -1;
    
    bencode_value_t *args = bencode_dict_new();
    if (!args) {
        bencode_free(dict);
        return -1;
    }
    
    char *tid = encode_transaction_id(state->next_transaction_id);
    if (!tid) {
        bencode_free(dict);
        bencode_free(args);
        return -1;
    }
    
    bencode_dict_add(args, "id", bencode_string_new((const char *)state->self_id, 20));
    bencode_dict_add(args, "target", bencode_string_new((const char *)target, 20));
    bencode_dict_add(dict, "t", bencode_string_new(tid, 4));
    bencode_dict_add(dict, "y", bencode_string_new("q", 1));
    mainline_tag_ro(state, dict);
    bencode_dict_add(dict, "q", bencode_string_new("find_node", 9));
    bencode_dict_add(dict, "a", args);
    
    free(tid);
    
    size_t len;
    char *data = bencode_encode(dict, &len);
    bencode_free(dict);
    
    if (!data) return -1;
    
    int result = net_send(state->net, (const uint8_t *)data, len, ip, port);
    free(data);
    
    if (result == 0) {
        if (state->transaction_count < MAINLINE_MAX_TRANSACTIONS) {
            state->transactions[state->transaction_count].id = state->next_transaction_id;
            state->transactions[state->transaction_count].created = time(NULL);
            state->transactions[state->transaction_count].is_put = 0;
            state->transactions[state->transaction_count].is_mutable = 0;
            /* find_node targets are 20-byte node ids; the transaction field is 32
             * (for the BEP-44 pubkey path). Copy 20 + zero the rest, not 32 (which
             * over-read the caller's 20-byte target — BUG-036). */
            memcpy(state->transactions[state->transaction_count].target, target, 20);
            memset(state->transactions[state->transaction_count].target + 20, 0, 12);
            state->transaction_count++;
        }
        state->next_transaction_id++;
    }
    
    return result;
}


/* ============================================================================
 * Synchronous get_peers / announce_peer over the public mainline DHT.
 *
 * Runs an iterative lookup toward info_hash on its own UDP socket (so it does
 * not race the daemon's main receive loop). Used to (a) advertise this node
 * under info_hash = hash(account) and (b) resolve another account's endpoint.
 * Returns: with do_announce==0, 1 if a peer endpoint was found (fills
 * peer_ip/peer_port) else 0; with do_announce==1, the number of nodes announced
 * to. Returns -1 on error.
 * ==========================================================================*/

#define LOOK_MAX_CAND 64
#define LOOK_ALPHA 6    /* parallel get_peers per round (wider/faster convergence) */
#define LOOK_K 8        /* announce to the K closest token-bearing nodes (BEP-5 K) */

typedef struct {
    unsigned char id[20];
    uint32_t ip;
    uint16_t port;
    unsigned char token[64];
    size_t token_len;
    int queried;
    int has_token;
} look_cand_t;

/* <0 if id_a is closer to target than id_b (XOR metric) */
static int look_closer(const unsigned char *target, const unsigned char *a, const unsigned char *b) {
    for (int i = 0; i < 20; i++) {
        unsigned char da = a[i] ^ target[i];
        unsigned char db = b[i] ^ target[i];
        if (da != db) return (int)da - (int)db;
    }
    return 0;
}

/* insert (id,ip,port) keeping cand[] sorted by distance to target, deduped */
static void look_insert(look_cand_t *cand, int *count, const unsigned char *target,
                        const unsigned char *id, uint32_t ip, uint16_t port) {
    if (ip == 0 || port == 0) return;
    for (int i = 0; i < *count; i++) {
        if (memcmp(cand[i].id, id, 20) == 0) return;
    }
    if (*count >= LOOK_MAX_CAND) {
        int far = *count - 1;
        if (look_closer(target, id, cand[far].id) >= 0) return;  /* not closer than farthest */
        *count = far;  /* drop farthest */
    }
    int pos = *count;
    while (pos > 0 && look_closer(target, id, cand[pos - 1].id) < 0) {
        cand[pos] = cand[pos - 1];
        pos--;
    }
    memset(&cand[pos], 0, sizeof(cand[pos]));
    memcpy(cand[pos].id, id, 20);
    cand[pos].ip = ip;
    cand[pos].port = port;
    (*count)++;
}

static char *look_build_get_peers(mainline_state_t *state, const unsigned char *info_hash,
                                  const char *tid, size_t *out_len) {
    bencode_value_t *dict = bencode_dict_new();
    bencode_value_t *args = bencode_dict_new();
    if (!dict || !args) { if (dict) bencode_free(dict); if (args) bencode_free(args); return NULL; }
    bencode_dict_add(args, "id", bencode_string_new((const char *)state->self_id, 20));
    bencode_dict_add(args, "info_hash", bencode_string_new((const char *)info_hash, 20));
    bencode_dict_add(dict, "t", bencode_string_new(tid, 2));
    bencode_dict_add(dict, "y", bencode_string_new("q", 1));
    mainline_tag_ro(state, dict);
    bencode_dict_add(dict, "q", bencode_string_new("get_peers", 9));
    bencode_dict_add(dict, "a", args);
    char *data = bencode_encode(dict, out_len);
    bencode_free(dict);
    return data;
}

static char *look_build_announce(mainline_state_t *state, const unsigned char *info_hash,
                                 uint16_t port, const unsigned char *token, size_t token_len,
                                 const char *tid, size_t *out_len) {
    bencode_value_t *dict = bencode_dict_new();
    bencode_value_t *args = bencode_dict_new();
    if (!dict || !args) { if (dict) bencode_free(dict); if (args) bencode_free(args); return NULL; }
    bencode_dict_add(args, "id", bencode_string_new((const char *)state->self_id, 20));
    bencode_dict_add(args, "info_hash", bencode_string_new((const char *)info_hash, 20));
    /* implied_port=1: the storing node records our packet's source ip:port,
     * which (when sent from the main DHT socket) is the live NAT mapping peers
     * will actually reach -- not our private bound port. */
    bencode_dict_add(args, "implied_port", bencode_int_new(1));
    bencode_dict_add(args, "port", bencode_int_new(port));
    bencode_dict_add(args, "token", bencode_string_new((const char *)token, token_len));
    /* norn extension: publish our ed25519 pubkey so peers resolving this
     * account learn its identity (-> VPN ULA) without contacting us directly
     * (enables routing/relay by identity). */
    if (state->have_self_pub)
        bencode_dict_add(args, "pk", bencode_string_new((const char *)state->self_pub, 32));
    bencode_dict_add(dict, "t", bencode_string_new(tid, 2));
    bencode_dict_add(dict, "y", bencode_string_new("q", 1));
    mainline_tag_ro(state, dict);
    bencode_dict_add(dict, "q", bencode_string_new("announce_peer", 13));
    bencode_dict_add(dict, "a", args);
    char *data = bencode_encode(dict, out_len);
    bencode_free(dict);
    return data;
}

static int serve_lookup_local(mainline_state_t *s, const unsigned char *info_hash,
                              uint32_t *ip, uint16_t *port);
static const unsigned char *serve_pubkey(mainline_state_t *s, const unsigned char *info_hash);

int mainline_lookup_ex(mainline_state_t *state, const unsigned char *info_hash,
                       int do_announce, uint16_t announce_port,
                       uint32_t *peer_ip, uint16_t *peer_port,
                       unsigned char *peer_pub, int timeout_ms,
                       mainline_log_func_t logf) {
    if (!state || !info_hash) return -1;

    /* If we ourselves store a peer for this info_hash (e.g. in a small private
     * overlay where announces land on our own nodes), answer without a network
     * lookup. */
    if (!do_announce && serve_lookup_local(state, info_hash, peer_ip, peer_port)) {
        const unsigned char *pk = serve_pubkey(state, info_hash);
        if (pk && peer_pub) memcpy(peer_pub, pk, 32);
        if (logf) logf("dht lookup: served from local store");
        return 1;
    }

    look_cand_t *cand = calloc(LOOK_MAX_CAND, sizeof(look_cand_t));
    if (!cand) return -1;
    int count = 0;
    for (int i = 0; i < state->node_count; i++) {
        look_insert(cand, &count, info_hash, state->nodes[i].id, state->nodes[i].ip, state->nodes[i].port);
    }
    if (count == 0) {
        if (logf) logf("dht lookup: routing table empty, cannot search");
        free(cand);
        return 0;
    }

    /* Run the get_peers lookup on a clean socket so it isn't drowned out by the
     * crawl traffic on the main socket. announce_peer (below) is sent from the
     * main socket instead, so the recorded source port is the main NAT mapping
     * peers actually reach; tokens are IP-based so they remain valid across our
     * two sockets (same public IP). */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { free(cand); return -1; }

    const char gp_tid[2] = { 'g', 'p' };
    int found = 0, announced = 0;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
        if (elapsed >= timeout_ms) break;
        if (found && !do_announce) break;

        int sent = 0;
        for (int i = 0; i < count && sent < LOOK_ALPHA; i++) {
            if (cand[i].queried) continue;
            cand[i].queried = 1;
            size_t qlen;
            char *q = look_build_get_peers(state, info_hash, gp_tid, &qlen);
            if (q) {
                struct sockaddr_in dst; memset(&dst, 0, sizeof(dst));
                dst.sin_family = AF_INET; dst.sin_addr.s_addr = cand[i].ip; dst.sin_port = htons(cand[i].port);
                sendto(sock, q, qlen, 0, (struct sockaddr *)&dst, sizeof(dst));
                free(q);
                sent++;
            }
        }
        if (sent == 0) break;  /* every candidate has been queried */

        for (;;) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
            long remaining = timeout_ms - elapsed;
            if (remaining <= 0) break;
            long window = remaining < 800 ? remaining : 800;
            struct timeval tv; tv.tv_sec = window / 1000; tv.tv_usec = (window % 1000) * 1000;
            fd_set rf; FD_ZERO(&rf); FD_SET(sock, &rf);
            if (select(sock + 1, &rf, NULL, NULL, &tv) <= 0) break;

            uint8_t buf[2048];
            struct sockaddr_in src; socklen_t srclen = sizeof(src);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &srclen);
            if (n <= 0) break;
            uint32_t from_ip = src.sin_addr.s_addr;
            uint16_t from_port = ntohs(src.sin_port);

            size_t pos = 0;
            bencode_value_t *msg = bencode_decode((const char *)buf, (size_t)n, &pos);
            if (!msg) continue;
            bencode_value_t *y = bencode_dict_get(msg, "y");
            bencode_value_t *r = bencode_dict_get(msg, "r");
            if (!y || y->type != BENCODE_STRING || y->val.str_val.len < 1 ||
                y->val.str_val.data[0] != 'r' || !r || r->type != BENCODE_DICT) {
                bencode_free(msg);
                continue;
            }
            for (int i = 0; i < count; i++) {
                if (cand[i].ip == from_ip && cand[i].port == from_port) {
                    bencode_value_t *tok = bencode_dict_get(r, "token");
                    if (tok && tok->type == BENCODE_STRING && tok->val.str_val.len <= sizeof(cand[i].token)) {
                        memcpy(cand[i].token, tok->val.str_val.data, tok->val.str_val.len);
                        cand[i].token_len = tok->val.str_val.len;
                        cand[i].has_token = 1;
                    }
                    break;
                }
            }
            bencode_value_t *values = bencode_dict_get(r, "values");
            if (values && values->type == BENCODE_LIST) {
                for (size_t i = 0; i < values->val.list_val.count; i++) {
                    bencode_value_t *v = values->val.list_val.items[i];
                    if (v && v->type == BENCODE_STRING && v->val.str_val.len >= 6) {
                        uint32_t pip; uint16_t pport;
                        memcpy(&pip, v->val.str_val.data, 4);
                        memcpy(&pport, v->val.str_val.data + 4, 2);
                        pport = ntohs(pport);
                        if (pip != 0 && pport != 0 && !found) {
                            if (peer_ip) *peer_ip = pip;
                            if (peer_port) *peer_port = pport;
                            found = 1;
                            if (logf) logf("dht lookup: found peer %s:%u", net_ip_to_str(pip), pport);
                        }
                    }
                }
            }
            /* norn extension: the account's published ed25519 pubkey */
            bencode_value_t *pkv = bencode_dict_get(r, "pk");
            if (pkv && pkv->type == BENCODE_STRING && pkv->val.str_val.len == 32 && peer_pub)
                memcpy(peer_pub, pkv->val.str_val.data, 32);
            bencode_value_t *nodes = bencode_dict_get(r, "nodes");
            if (nodes && nodes->type == BENCODE_STRING) {
                for (size_t i = 0; i + 26 <= nodes->val.str_val.len; i += 26) {
                    const char *p = nodes->val.str_val.data + i;
                    uint32_t nip; uint16_t nport;
                    memcpy(&nip, p + 20, 4);
                    memcpy(&nport, p + 24, 2);
                    nport = ntohs(nport);
                    look_insert(cand, &count, info_hash, (const unsigned char *)p, nip, nport);
                }
            }
            bencode_free(msg);
            if (found && !do_announce) break;
        }
    }

    if (do_announce) {
        const char an_tid[2] = { 'a', 'p' };
        for (int i = 0, done = 0; i < count && done < LOOK_K; i++) {
            if (!cand[i].has_token) continue;
            size_t qlen;
            char *q = look_build_announce(state, info_hash, announce_port,
                                          cand[i].token, cand[i].token_len, an_tid, &qlen);
            if (q) {
                struct sockaddr_in dst; memset(&dst, 0, sizeof(dst));
                dst.sin_family = AF_INET; dst.sin_addr.s_addr = cand[i].ip; dst.sin_port = htons(cand[i].port);
                /* send announce from the MAIN socket so the recorded source
                 * port is the live, reachable NAT mapping (implied_port=1). */
                sendto(state->net->fd, q, qlen, 0, (struct sockaddr *)&dst, sizeof(dst));
                free(q);
                announced++;
                done++;
            }
        }
        if (logf) logf("norn announce: sent to %d node(s) from main socket", announced);
    }

    close(sock);
    free(cand);
    return found ? 1 : (do_announce ? announced : 0);
}

int mainline_lookup(mainline_state_t *state, const unsigned char *info_hash,
                    int do_announce, uint16_t announce_port,
                    uint32_t *peer_ip, uint16_t *peer_port, int timeout_ms,
                    mainline_log_func_t logf) {
    return mainline_lookup_ex(state, info_hash, do_announce, announce_port,
                              peer_ip, peer_port, NULL, timeout_ms, logf);
}

/* BEP-44 get query: a = { id, target }. */
static char *look_build_get_mutable(mainline_state_t *state, const unsigned char *target,
                                    const char *tid, size_t *out_len) {
    bencode_value_t *dict = bencode_dict_new();
    bencode_value_t *args = bencode_dict_new();
    if (!dict || !args) { if (dict) bencode_free(dict); if (args) bencode_free(args); return NULL; }
    bencode_dict_add(args, "id", bencode_string_new((const char *)state->self_id, 20));
    bencode_dict_add(args, "target", bencode_string_new((const char *)target, 20));
    bencode_dict_add(dict, "t", bencode_string_new(tid, 2));
    bencode_dict_add(dict, "y", bencode_string_new("q", 1));
    mainline_tag_ro(state, dict);
    bencode_dict_add(dict, "q", bencode_string_new("get", 3));
    bencode_dict_add(dict, "a", args);
    char *data = bencode_encode(dict, out_len);
    bencode_free(dict);
    return data;
}

/* BEP-44 put query: a = { id, k, seq, sig, v, token, [salt] }. salt (when set)
 * names a per-key signed item under SHA1(k||salt) — used by `spub`. */
static char *look_build_put_mutable(mainline_state_t *state, const unsigned char *target,
                                    const unsigned char *k, uint32_t seq,
                                    const unsigned char *sig, const unsigned char *v, size_t vlen,
                                    const unsigned char *salt, size_t saltlen, int immutable,
                                    const unsigned char *token, size_t token_len,
                                    const char *tid, size_t *out_len) {
    bencode_value_t *dict = bencode_dict_new();
    bencode_value_t *args = bencode_dict_new();
    if (!dict || !args) { if (dict) bencode_free(dict); if (args) bencode_free(args); return NULL; }
    bencode_dict_add(args, "id", bencode_string_new((const char *)state->self_id, 20));
    if (k) {
        /* signed/mutable (BEP-44) put: k + sig [+ salt] + seq */
        bencode_dict_add(args, "k", bencode_string_new((const char *)k, 32));
        bencode_dict_add(args, "sig", bencode_string_new((const char *)sig, 64));
        if (salt && saltlen) bencode_dict_add(args, "salt", bencode_string_new((const char *)salt, saltlen));
        bencode_dict_add(args, "seq", bencode_int_new((long)seq));
    } else {
        /* BEP-44 immutable put (`ipub`): just v — the responder keys it by
         * SHA1(bencode(v)) itself; no k/sig/target/seq. */
        (void)immutable; (void)target;
    }
    bencode_dict_add(args, "v", bencode_string_new((const char *)v, vlen));
    bencode_dict_add(args, "token", bencode_string_new((const char *)token, token_len));
    bencode_dict_add(dict, "t", bencode_string_new(tid, 2));
    bencode_dict_add(dict, "y", bencode_string_new("q", 1));
    bencode_dict_add(dict, "q", bencode_string_new("put", 3));
    bencode_dict_add(dict, "a", args);
    char *data = bencode_encode(dict, out_len);
    bencode_free(dict);
    return data;
}

/* Iterative BEP-44 client (BPE-0004 stage 4c). Walks toward target=SHA1(k) with
 * `get` queries (collecting write tokens and any stored record — verified +
 * stored via recstore_accept). With do_put, then `put`s {k,seq,sig,v,token} to
 * the closest token-bearing nodes. Returns nodes-put-to (do_put) or 1/0 (get,
 * whether a record was accepted). */
int mainline_lookup_mutable(mainline_state_t *state, const unsigned char *target,
                            int do_put, const unsigned char *k, uint32_t seq,
                            const unsigned char *v, size_t vlen, const unsigned char *sig,
                            const unsigned char *salt, size_t saltlen, int immutable,
                            unsigned char *value_out, size_t *vlen_out, size_t vcap,
                            int timeout_ms, mainline_log_func_t logf) {
    if (!state || !target) return -1;
    look_cand_t *cand = calloc(LOOK_MAX_CAND, sizeof(look_cand_t));
    if (!cand) return -1;
    int count = 0;
    for (int i = 0; i < state->node_count; i++)
        look_insert(cand, &count, target, state->nodes[i].id, state->nodes[i].ip, state->nodes[i].port);
    if (count == 0) { free(cand); return 0; }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { free(cand); return -1; }
    const char tid[2] = { 'm', 'g' };
    int accepted = 0, put = 0;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
        if (elapsed >= timeout_ms) break;

        int sent = 0;
        for (int i = 0; i < count && sent < LOOK_ALPHA; i++) {
            if (cand[i].queried) continue;
            cand[i].queried = 1;
            size_t qlen; char *q = look_build_get_mutable(state, target, tid, &qlen);
            if (q) {
                struct sockaddr_in dst; memset(&dst, 0, sizeof(dst));
                dst.sin_family = AF_INET; dst.sin_addr.s_addr = cand[i].ip; dst.sin_port = htons(cand[i].port);
                sendto(sock, q, qlen, 0, (struct sockaddr *)&dst, sizeof(dst));
                free(q); sent++;
            }
        }
        if (sent == 0) break;

        for (;;) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
            long remaining = timeout_ms - elapsed;
            if (remaining <= 0) break;
            long window = remaining < 800 ? remaining : 800;
            struct timeval tv; tv.tv_sec = window / 1000; tv.tv_usec = (window % 1000) * 1000;
            fd_set rf; FD_ZERO(&rf); FD_SET(sock, &rf);
            if (select(sock + 1, &rf, NULL, NULL, &tv) <= 0) break;
            uint8_t buf[2048];
            struct sockaddr_in src; socklen_t sl = sizeof(src);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &sl);
            if (n <= 0) break;
            uint32_t fip = src.sin_addr.s_addr; uint16_t fport = ntohs(src.sin_port);
            size_t pos = 0;
            bencode_value_t *msg = bencode_decode((const char *)buf, (size_t)n, &pos);
            if (!msg) continue;
            bencode_value_t *y = bencode_dict_get(msg, "y");
            bencode_value_t *r = bencode_dict_get(msg, "r");
            if (!y || y->type != BENCODE_STRING || y->val.str_val.data[0] != 'r' ||
                !r || r->type != BENCODE_DICT) { bencode_free(msg); continue; }
            /* record the write token from this node */
            for (int i = 0; i < count; i++)
                if (cand[i].ip == fip && cand[i].port == fport) {
                    bencode_value_t *tok = bencode_dict_get(r, "token");
                    if (tok && tok->type == BENCODE_STRING && tok->val.str_val.len <= sizeof(cand[i].token)) {
                        memcpy(cand[i].token, tok->val.str_val.data, tok->val.str_val.len);
                        cand[i].token_len = tok->val.str_val.len; cand[i].has_token = 1;
                    }
                    break;
                }
            bencode_value_t *rv = bencode_dict_get(r, "v");
            /* immutable get (`iget`): the response carries just 'v' (no k/seq/sig).
             * Verify content-addressing (SHA1(bencode(v)) == target) and return it. */
            if (value_out && immutable && !accepted &&
                rv && rv->type == BENCODE_STRING && rv->val.str_val.len > 0) {
                const unsigned char *gv = (const unsigned char *)rv->val.str_val.data;
                size_t gvl = rv->val.str_val.len;
                unsigned char chk[20];
                if (bep44_immutable_target(gv, gvl, chk) == 0 && memcmp(chk, target, 20) == 0) {
                    size_t cp = gvl < vcap ? gvl : (vcap ? vcap - 1 : 0);
                    if (vcap) { memcpy(value_out, gv, cp); value_out[cp] = '\0'; }
                    if (vlen_out) *vlen_out = cp;
                    accepted = 1;
                    if (logf) logf("dht: fetched an immutable value (%zu bytes)", gvl);
                }
            }
            /* a returned record: verify + store via the shared gate */
            bencode_value_t *rk = bencode_dict_get(r, "k");
            bencode_value_t *rs = bencode_dict_get(r, "sig");
            bencode_value_t *rq = bencode_dict_get(r, "seq");
            if (!immutable &&
                rk && rk->type == BENCODE_STRING && rk->val.str_val.len == 32 &&
                rs && rs->type == BENCODE_STRING && rs->val.str_val.len == 64 &&
                rq && rq->type == BENCODE_INT &&
                rv && rv->type == BENCODE_STRING && rv->val.str_val.len > 0) {
                const unsigned char *gk = (const unsigned char *)rk->val.str_val.data;
                const unsigned char *gv = (const unsigned char *)rv->val.str_val.data;
                size_t gvl = rv->val.str_val.len;
                const unsigned char *gs = (const unsigned char *)rs->val.str_val.data;
                uint32_t gseq = (uint32_t)rq->val.int_val;
                if (value_out) {
                    /* Mutable KV get (mget, k set): verify the SALTED signature against
                     * the account key we asked for, then return the value to the caller
                     * (immutable gets are handled by the v-only branch above). */
                    unsigned char sb[64 + 1024]; size_t cap = gvl <= 1024 ? gvl : 1024;
                    int bn = bep44_signbuf_salted(salt, saltlen, gseq, gv, cap, sb, sizeof(sb));
                    if (k && bn > 0 && memcmp(gk, k, 32) == 0 && bf_verify(gs, sb, (size_t)bn, gk) == 0) {
                        size_t cp = gvl < vcap ? gvl : (vcap ? vcap - 1 : 0);
                        if (vcap) { memcpy(value_out, gv, cp); value_out[cp] = '\0'; }
                        if (vlen_out) *vlen_out = cp;
                        accepted = 1;
                        if (logf) logf("dht: fetched a signed value (seq=%u, %zu bytes)", gseq, gvl);
                    }
                } else if (recstore_accept(gk, gseq, gv, gvl, gs)) {
                    accepted = 1;
                    if (logf) logf("dht: fetched a signed record (seq=%ld)", rq->val.int_val);
                }
            }
            bencode_value_t *nodes = bencode_dict_get(r, "nodes");
            if (nodes && nodes->type == BENCODE_STRING)
                for (size_t i = 0; i + 26 <= nodes->val.str_val.len; i += 26) {
                    const char *p = nodes->val.str_val.data + i;
                    uint32_t nip; uint16_t nport;
                    memcpy(&nip, p + 20, 4); memcpy(&nport, p + 24, 2); nport = ntohs(nport);
                    look_insert(cand, &count, target, (const unsigned char *)p, nip, nport);
                }
            bencode_free(msg);
        }
    }

    if (do_put && k && v && sig) {
        const char ptid[2] = { 'm', 'p' };
        for (int i = 0, done = 0; i < count && done < LOOK_K; i++) {
            if (!cand[i].has_token) continue;
            size_t qlen;
            char *q = look_build_put_mutable(state, target, k, seq, sig, v, vlen, salt, saltlen,
                                             immutable, cand[i].token, cand[i].token_len, ptid, &qlen);
            if (q) {
                struct sockaddr_in dst; memset(&dst, 0, sizeof(dst));
                dst.sin_family = AF_INET; dst.sin_addr.s_addr = cand[i].ip; dst.sin_port = htons(cand[i].port);
                sendto(sock, q, qlen, 0, (struct sockaddr *)&dst, sizeof(dst));
                free(q); put++; done++;
            }
        }
        if (logf) logf("dht: published record to %d node(s)", put);
    }
    close(sock);
    free(cand);
    return do_put ? put : (accepted ? 1 : 0);
}

/* dedup-append (ip,port) to the result arrays */
/* find_node query for `target` (q=find_node, a={id, target}). */
static char *look_build_find_node(mainline_state_t *state, const unsigned char *target,
                                  const char *tid, size_t *out_len) {
    bencode_value_t *dict = bencode_dict_new();
    bencode_value_t *args = bencode_dict_new();
    if (!dict || !args) { if (dict) bencode_free(dict); if (args) bencode_free(args); return NULL; }
    bencode_dict_add(args, "id", bencode_string_new((const char *)state->self_id, 20));
    bencode_dict_add(args, "target", bencode_string_new((const char *)target, 20));
    bencode_dict_add(dict, "t", bencode_string_new(tid, 2));
    bencode_dict_add(dict, "y", bencode_string_new("q", 1));
    mainline_tag_ro(state, dict);
    bencode_dict_add(dict, "q", bencode_string_new("find_node", 9));
    bencode_dict_add(dict, "a", args);
    char *data = bencode_encode(dict, out_len);
    bencode_free(dict);
    return data;
}

/* Resolve an account by its node_id (= SHA256(account)[:20]). Each node sits at
 * its own account's node_id, so an iterative find_node converges to that node and
 * its contact (ip:port) comes straight from the DHT routing layer — there is no
 * announce_peer/get_peers "values" path, and therefore none of the unrelated
 * BitTorrent peers those attract on the shared public DHT. Prefers the address
 * from which the TARGET itself answers (a path we KNOW is reachable) over its
 * contact as merely recorded by other nodes. Returns 1 and fills ip/port, else 0. */
int mainline_resolve_node(mainline_state_t *state, const unsigned char *node_id,
                          uint32_t *ip_out, uint16_t *port_out, int timeout_ms,
                          mainline_log_func_t logf, int *confirmed_out) {
    if (confirmed_out) *confirmed_out = 0;
    if (!state || !node_id || !ip_out || !port_out) return 0;

    look_cand_t *cand = calloc(LOOK_MAX_CAND, sizeof(look_cand_t));
    if (!cand) return 0;
    int count = 0;
    for (int i = 0; i < state->node_count; i++)
        look_insert(cand, &count, node_id, state->nodes[i].id, state->nodes[i].ip, state->nodes[i].port);
    if (count == 0) { free(cand); return 0; }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { free(cand); return 0; }

    const char fn_tid[2] = { 'f', 'n' };
    uint32_t direct_ip = 0, contact_ip = 0;      /* direct = target answered itself */
    uint16_t direct_port = 0, contact_port = 0;  /* contact = recorded by other nodes */
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
        if (elapsed >= timeout_ms || direct_ip) break;

        int sent = 0;
        for (int i = 0; i < count && sent < LOOK_ALPHA; i++) {
            if (cand[i].queried) continue;
            cand[i].queried = 1;
            size_t qlen;
            char *q = look_build_find_node(state, node_id, fn_tid, &qlen);
            if (q) {
                struct sockaddr_in dst; memset(&dst, 0, sizeof(dst));
                dst.sin_family = AF_INET; dst.sin_addr.s_addr = cand[i].ip; dst.sin_port = htons(cand[i].port);
                sendto(sock, q, qlen, 0, (struct sockaddr *)&dst, sizeof(dst));
                free(q);
                sent++;
            }
        }
        if (sent == 0) break;  /* every candidate queried */

        for (;;) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
            long remaining = timeout_ms - elapsed;
            if (remaining <= 0) break;
            long window = remaining < 800 ? remaining : 800;
            struct timeval tv; tv.tv_sec = window / 1000; tv.tv_usec = (window % 1000) * 1000;
            fd_set rf; FD_ZERO(&rf); FD_SET(sock, &rf);
            if (select(sock + 1, &rf, NULL, NULL, &tv) <= 0) break;

            uint8_t buf[2048];
            struct sockaddr_in src; socklen_t srclen = sizeof(src);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &srclen);
            if (n <= 0) break;
            uint32_t from_ip = src.sin_addr.s_addr;
            uint16_t from_port = ntohs(src.sin_port);

            size_t pos = 0;
            bencode_value_t *msg = bencode_decode((const char *)buf, (size_t)n, &pos);
            if (!msg) continue;
            bencode_value_t *y = bencode_dict_get(msg, "y");
            bencode_value_t *r = bencode_dict_get(msg, "r");
            if (!y || y->type != BENCODE_STRING || y->val.str_val.len < 1 ||
                y->val.str_val.data[0] != 'r' || !r || r->type != BENCODE_DICT) {
                bencode_free(msg); continue;
            }
            /* Did the TARGET itself answer? Its source is a path we can reach. */
            bencode_value_t *rid = bencode_dict_get(r, "id");
            if (rid && rid->type == BENCODE_STRING && rid->val.str_val.len >= 20 &&
                memcmp(rid->val.str_val.data, node_id, 20) == 0) {
                direct_ip = from_ip; direct_port = from_port;
            }
            /* Otherwise note the target's contact from the returned node list. */
            bencode_value_t *nodes = bencode_dict_get(r, "nodes");
            if (nodes && nodes->type == BENCODE_STRING) {
                for (size_t i = 0; i + 26 <= nodes->val.str_val.len; i += 26) {
                    const char *p = nodes->val.str_val.data + i;
                    uint32_t nip; uint16_t nport;
                    memcpy(&nip, p + 20, 4);
                    memcpy(&nport, p + 24, 2);
                    nport = ntohs(nport);
                    if (memcmp(p, node_id, 20) == 0 && contact_ip == 0) { contact_ip = nip; contact_port = nport; }
                    look_insert(cand, &count, node_id, (const unsigned char *)p, nip, nport);
                }
            }
            bencode_free(msg);
            if (direct_ip) break;
        }
    }
    close(sock);
    free(cand);

    if (direct_ip) {
        *ip_out = direct_ip; *port_out = direct_port;
        if (confirmed_out) *confirmed_out = 1;
        if (logf) logf("resolve: target answered from %s:%u", net_ip_to_str(direct_ip), direct_port);
        return 1;
    }
    if (contact_ip) {
        *ip_out = contact_ip; *port_out = contact_port;   /* confirmed stays 0 */
        if (logf) logf("resolve: target contact %s:%u (via routing, unconfirmed)",
                       net_ip_to_str(contact_ip), contact_port);
        return 1;
    }
    return 0;
}

/* ---- DHT-server side: tokens + announced-peer store (BEP-5) ---------------- */

/* Token tied to the requester's IP and our per-run secret (keyed CRC32C; not
 * cryptographic, but adequate to stop trivial announce spoofing — a real impl
 * would HMAC). */
/* BEP-5 token = keyed BLAKE2b over (ip || epoch), so it can't be forged without the
 * secret (CRC32C was linear/forgeable) and rotates every TOKEN_EPOCH seconds
 * (BUG-015). validate accepts the current and previous epoch for a grace window. */
#define TOKEN_EPOCH 300
static void serve_token_ep(mainline_state_t *s, uint32_t ip, uint64_t epoch, unsigned char out[4]) {
    unsigned char in[12], full[crypto_generichash_BYTES_MIN];
    memcpy(in, &ip, 4);
    memcpy(in + 4, &epoch, 8);
    crypto_generichash(full, sizeof(full), in, sizeof(in), s->token_secret, 16);
    memcpy(out, full, 4);
}
static void serve_token(mainline_state_t *s, uint32_t ip, unsigned char out[4]) {
    serve_token_ep(s, ip, (uint64_t)(time(NULL) / TOKEN_EPOCH), out);
}

static int serve_token_valid(mainline_state_t *s, uint32_t ip, const char *tok, size_t len) {
    if (!tok || len < 4) return 0;
    uint64_t now = (uint64_t)(time(NULL) / TOKEN_EPOCH);
    unsigned char e[4];
    serve_token_ep(s, ip, now, e);     if (memcmp(e, tok, 4) == 0) return 1;
    serve_token_ep(s, ip, now - 1, e); if (memcmp(e, tok, 4) == 0) return 1;  /* grace */
    return 0;
}

static void serve_store_peer(mainline_state_t *s, const unsigned char *info_hash,
                             uint32_t ip, uint16_t port, const unsigned char *pubkey) {
    if (ip == 0 || port == 0) return;
    int idx = -1;
    for (int i = 0; i < s->served_count; i++)
        if (memcmp(s->served[i].info_hash, info_hash, 20) == 0) { idx = i; break; }
    if (idx < 0) {
        idx = (s->served_count < MAINLINE_SERVE_HASHES) ? s->served_count++ : 0; /* evict 0 if full */
        memcpy(s->served[idx].info_hash, info_hash, 20);
        s->served[idx].peer_count = 0;
        s->served[idx].have_pubkey = 0;
    }
    if (pubkey) { memcpy(s->served[idx].pubkey, pubkey, 32); s->served[idx].have_pubkey = 1; }
    for (int i = 0; i < s->served[idx].peer_count; i++)
        if (s->served[idx].peers[i].ip == ip && s->served[idx].peers[i].port == port) return;
    if (s->served[idx].peer_count < MAINLINE_SERVE_PEERS) {
        s->served[idx].peers[s->served[idx].peer_count].ip = ip;
        s->served[idx].peers[s->served[idx].peer_count].port = port;
        s->served[idx].peer_count++;
    } else {
        memmove(&s->served[idx].peers[0], &s->served[idx].peers[1],
                (MAINLINE_SERVE_PEERS - 1) * sizeof(s->served[idx].peers[0]));
        s->served[idx].peers[MAINLINE_SERVE_PEERS - 1].ip = ip;
        s->served[idx].peers[MAINLINE_SERVE_PEERS - 1].port = port;
    }
}

/* If we store any peer for info_hash, return its first endpoint. */
static int serve_lookup_local(mainline_state_t *s, const unsigned char *info_hash,
                              uint32_t *ip, uint16_t *port) {
    for (int i = 0; i < s->served_count; i++) {
        if (memcmp(s->served[i].info_hash, info_hash, 20) == 0 && s->served[i].peer_count > 0) {
            if (ip) *ip = s->served[i].peers[0].ip;
            if (port) *port = s->served[i].peers[0].port;
            return 1;
        }
    }
    return 0;
}

/* The published ed25519 pubkey we hold for info_hash, or NULL. We answer
 * authoritatively for our own account. */
static const unsigned char *serve_pubkey(mainline_state_t *s, const unsigned char *info_hash) {
    if (s->have_self_account && s->have_self_pub &&
        memcmp(info_hash, s->self_account, 20) == 0)
        return s->self_pub;
    for (int i = 0; i < s->served_count; i++)
        if (memcmp(s->served[i].info_hash, info_hash, 20) == 0 && s->served[i].have_pubkey)
            return s->served[i].pubkey;
    return NULL;
}

/* Build a bencode list of compact (6-byte) peers for info_hash, or NULL. For
 * OUR OWN account we serve our reflexive endpoint directly, so any reachable
 * node (e.g. a relay hub) resolves without waiting for overlay convergence. */
static bencode_value_t *serve_values(mainline_state_t *s, const unsigned char *info_hash) {
    bencode_value_t *list = NULL;
    if (s->have_self_account && memcmp(info_hash, s->self_account, 20) == 0) {
        uint32_t eip; uint16_t eport;
        if (net_get_external_endpoint(s->net, &eip, &eport) == 0 && eip && eport) {
            list = bencode_list_new();
            if (list) {
                char cp[6];
                memcpy(cp, &eip, 4);
                uint16_t p = htons(eport);
                memcpy(cp + 4, &p, 2);
                bencode_list_add(list, bencode_string_new(cp, 6));
            }
        }
    }
    for (int i = 0; i < s->served_count; i++) {
        if (memcmp(s->served[i].info_hash, info_hash, 20) == 0 && s->served[i].peer_count > 0) {
            if (!list) list = bencode_list_new();
            if (!list) return NULL;
            for (int j = 0; j < s->served[i].peer_count; j++) {
                char cp[6];
                memcpy(cp, &s->served[i].peers[j].ip, 4);
                uint16_t p = htons(s->served[i].peers[j].port);
                memcpy(cp + 4, &p, 2);
                bencode_list_add(list, bencode_string_new(cp, 6));
            }
            break;
        }
    }
    return list;
}

/* Compact (26-byte) node info for up to `max` nodes closest to target. */
static char *serve_compact_closest(mainline_state_t *s, const unsigned char *target,
                                   size_t *out_len, int max) {
    int n = s->node_count;
    *out_len = 0;
    if (n <= 0) return NULL;
    if (max > n) max = n;
    int *idx = malloc((size_t)n * sizeof(int));
    if (!idx) return NULL;
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int a = 0; a < max; a++) {
        int best = a;
        for (int b = a + 1; b < n; b++)
            if (look_closer(target, s->nodes[idx[b]].id, s->nodes[idx[best]].id) < 0) best = b;
        int t = idx[a]; idx[a] = idx[best]; idx[best] = t;
    }
    char *buf = malloc((size_t)max * 26);
    if (!buf) { free(idx); return NULL; }
    for (int a = 0; a < max; a++) {
        char *p = buf + a * 26;
        memcpy(p, s->nodes[idx[a]].id, 20);
        memcpy(p + 20, &s->nodes[idx[a]].ip, 4);
        uint16_t pt = htons(s->nodes[idx[a]].port);
        memcpy(p + 24, &pt, 2);
    }
    free(idx);
    *out_len = (size_t)max * 26;
    return buf;
}

int mainline_process_packet(mainline_state_t *state, const uint8_t *data, size_t len, uint32_t from_ip, uint16_t from_port) {
    if (!state || !data || len == 0) return -1;
    
    size_t pos = 0;
    bencode_value_t *msg = bencode_decode((const char *)data, len, &pos);
    if (!msg) return -1;
    
    bencode_value_t *y_val = bencode_dict_get(msg, "y");
    if (!y_val || y_val->type != BENCODE_STRING) {
        bencode_free(msg);
        return -1;
    }

    /* BEP-43: a sender that tags its message "ro":1 is read-only — never insert
     * it into our routing table (it can't serve and may be transient/unreachable). */
    bencode_value_t *ro_val = bencode_dict_get(msg, "ro");
    int incoming_ro = (ro_val && ro_val->type == BENCODE_INT && ro_val->val.int_val != 0);

    if (strcmp(y_val->val.str_val.data, "r") == 0) {
        bencode_value_t *t_val = bencode_dict_get(msg, "t");
        if (!t_val || t_val->type != BENCODE_STRING) {
            bencode_free(msg);
            return -1;
        }
        
        uint32_t tid = decode_transaction_id(t_val->val.str_val.data, t_val->val.str_val.len);

        /* BUG-013: only accept a reply that matches an OUTSTANDING query we sent.
         * Without this, an off-path/unsolicited 'r' (any tid) injected nodes into
         * the routing table and reflexive-ip votes before the tid was ever checked.
         * Match + consume the transaction up front; drop anything unsolicited. */
        int txi = -1;
        for (int i = 0; i < state->transaction_count; i++)
            if (state->transactions[i].id == tid) { txi = i; break; }
        if (txi < 0) { bencode_free(msg); return -1; }
        for (int j = txi; j < state->transaction_count - 1; j++)
            state->transactions[j] = state->transactions[j + 1];
        state->transaction_count--;

        bencode_value_t *r = bencode_dict_get(msg, "r");
        if (!r || r->type != BENCODE_DICT) {
            bencode_free(msg);
            return -1;
        }

        /* Learn the responder itself (it answered us, so it's alive) — unless it
         * is read-only (BEP-43). */
        bencode_value_t *rid = bencode_dict_get(r, "id");
        if (!incoming_ro && rid && rid->type == BENCODE_STRING && rid->val.str_val.len >= 20)
            mainline_add_node(state, (const unsigned char *)rid->val.str_val.data, from_ip, from_port);
        
        /* BEP-42: the "ip" field is a top-level key holding our reflexive
         * (public) ip:port as seen by the responder. Feed it to the vote. */
        bencode_value_t *ip_val = bencode_dict_get(msg, "ip");
        if (ip_val && ip_val->type == BENCODE_STRING && ip_val->val.str_val.len >= 6) {
            uint32_t ip;
            uint16_t port;
            memcpy(&ip, ip_val->val.str_val.data, 4);
            memcpy(&port, ip_val->val.str_val.data + 4, 2);
            port = ntohs(port);
            net_update_external_ip(state->net, ip, port);
        }

        bencode_value_t *nodes = bencode_dict_get(r, "nodes");
        if (nodes && nodes->type == BENCODE_STRING) {
            int parsed_count = 0;
            int added_count = 0;
            for (size_t i = 0; i + 26 <= nodes->val.str_val.len; i += 26) {
                unsigned char node_id[20];
                memcpy(node_id, nodes->val.str_val.data + i, 20);
                uint32_t node_ip;
                memcpy(&node_ip, nodes->val.str_val.data + i + 20, 4);
                uint16_t np_be; memcpy(&np_be, nodes->val.str_val.data + i + 24, 2);  /* BUG-116: unaligned → SIGBUS on ARM */
                uint16_t node_port = ntohs(np_be);
                
                parsed_count++;
                int result = mainline_add_node(state, node_id, node_ip, node_port);
                if (result > 0) {
                    added_count++;
                }
            }
            if (added_count > 0)   /* BUG-099: stay quiet on the common "+0 new" case */
                ML_LOG("mainline: parsed %d nodes from %s:%d, +%d new (total %d)",
                       parsed_count, net_ip_to_str(from_ip), from_port, added_count, state->node_count);
        }
        
        /* (transaction already consumed above, before processing the reply) */
    } else if (strcmp(y_val->val.str_val.data, "q") == 0) {
        /* BEP-43: a read-only node does not answer queries. */
        if (state->read_only) {
            bencode_free(msg);
            return 0;
        }
        bencode_value_t *q_val = bencode_dict_get(msg, "q");
        if (!q_val || q_val->type != BENCODE_STRING) {
            bencode_free(msg);
            return -1;
        }
        
        bencode_value_t *a = bencode_dict_get(msg, "a");
        if (!a || a->type != BENCODE_DICT) {
            bencode_free(msg);
            return -1;
        }

        /* Learn the querier (it contacted us) — essential for the overlay to
         * form — unless it is read-only (BEP-43). */
        bencode_value_t *qid = bencode_dict_get(a, "id");
        if (!incoming_ro && qid && qid->type == BENCODE_STRING && qid->val.str_val.len >= 20)
            mainline_add_node(state, (const unsigned char *)qid->val.str_val.data, from_ip, from_port);

        bencode_value_t *t_val = bencode_dict_get(msg, "t");
        if (!t_val || t_val->type != BENCODE_STRING) {
            bencode_free(msg);
            return -1;
        }
        
        bencode_value_t *resp = bencode_dict_new();
        if (!resp) {
            bencode_free(msg);
            return -1;
        }
        
        bencode_value_t *r = bencode_dict_new();
        if (!r) {
            bencode_free(resp);
            bencode_free(msg);
            return -1;
        }
        
        const char *q = q_val->val.str_val.data;
        size_t qlen = q_val->val.str_val.len;
        bencode_dict_add(r, "id", bencode_string_new((const char *)state->self_id, 20));

        if (qlen == 4 && memcmp(q, "ping", 4) == 0) {
            /* id + our norn version, so a pinger learns the remote version */
            if (state->have_self_version)
                bencode_dict_add(r, "nv", bencode_string_new(state->self_version,
                                                             strlen(state->self_version)));
        } else if (qlen == 9 && memcmp(q, "find_node", 9) == 0) {
            bencode_value_t *t = bencode_dict_get(a, "target");
            const unsigned char *target =
                (t && t->type == BENCODE_STRING && t->val.str_val.len >= 20)
                    ? (const unsigned char *)t->val.str_val.data : state->self_id;
            size_t nlen = 0;
            char *nodes = serve_compact_closest(state, target, &nlen, 8);
            bencode_dict_add(r, "nodes", bencode_string_new(nodes ? nodes : "", nlen));
            free(nodes);
        } else if (qlen == 9 && memcmp(q, "get_peers", 9) == 0) {
            bencode_value_t *ihv = bencode_dict_get(a, "info_hash");
            unsigned char tok[4];
            serve_token(state, from_ip, tok);
            bencode_dict_add(r, "token", bencode_string_new((const char *)tok, 4));
            if (ihv && ihv->type == BENCODE_STRING && ihv->val.str_val.len >= 20) {
                const unsigned char *ih = (const unsigned char *)ihv->val.str_val.data;
                bencode_value_t *vals = serve_values(state, ih);
                if (vals) {
                    bencode_dict_add(r, "values", vals);
                } else {
                    size_t nlen = 0;
                    char *nodes = serve_compact_closest(state, ih, &nlen, 8);
                    bencode_dict_add(r, "nodes", bencode_string_new(nodes ? nodes : "", nlen));
                    free(nodes);
                }
                /* norn: also return the account's published pubkey if we hold it */
                const unsigned char *pk = serve_pubkey(state, ih);
                if (pk) bencode_dict_add(r, "pk", bencode_string_new((const char *)pk, 32));
            }
        } else if (qlen == 13 && memcmp(q, "announce_peer", 13) == 0) {
            bencode_value_t *ihv = bencode_dict_get(a, "info_hash");
            bencode_value_t *tokv = bencode_dict_get(a, "token");
            bencode_value_t *implied = bencode_dict_get(a, "implied_port");
            bencode_value_t *portv = bencode_dict_get(a, "port");
            bencode_value_t *pkv = bencode_dict_get(a, "pk");
            if (ihv && ihv->type == BENCODE_STRING && ihv->val.str_val.len >= 20 &&
                tokv && tokv->type == BENCODE_STRING &&
                serve_token_valid(state, from_ip, tokv->val.str_val.data, tokv->val.str_val.len)) {
                uint16_t port = from_port;
                int use_implied = implied && implied->type == BENCODE_INT && implied->val.int_val != 0;
                if (!use_implied && portv && portv->type == BENCODE_INT &&
                    portv->val.int_val > 0 && portv->val.int_val <= 65535) {
                    port = (uint16_t)portv->val.int_val;
                }
                const unsigned char *pk =
                    (pkv && pkv->type == BENCODE_STRING && pkv->val.str_val.len == 32)
                        ? (const unsigned char *)pkv->val.str_val.data : NULL;
                serve_store_peer(state, (const unsigned char *)ihv->val.str_val.data, from_ip, port, pk);
            }
            /* id-only response */
        } else if (qlen == 3 && memcmp(q, "get", 3) == 0) {
            /* BEP-44 get: return the stored mutable item, or closest nodes; always
             * a write token for a follow-up put. */
            bencode_value_t *tv = bencode_dict_get(a, "target");
            unsigned char tok[4]; serve_token(state, from_ip, tok);
            bencode_dict_add(r, "token", bencode_string_new((const char *)tok, 4));
            if (tv && tv->type == BENCODE_STRING && tv->val.str_val.len >= 20) {
                const unsigned char *tgt = (const unsigned char *)tv->val.str_val.data;
                unsigned char gk[32], gsig[64], gv[DHTSTORE_VMAX]; uint32_t gseq; size_t gvl; int gimm = 0;
                if (dhtstore_get_ex(tgt, gk, &gseq, gv, sizeof(gv), &gvl, gsig, &gimm)) {
                    if (gimm) {
                        /* immutable: BEP-44 says return just 'v' (content-addressed). */
                        bencode_dict_add(r, "v", bencode_string_new((const char *)gv, gvl));
                    } else {
                        bencode_dict_add(r, "k", bencode_string_new((const char *)gk, 32));
                        bencode_dict_add(r, "seq", bencode_int_new((long)gseq));
                        bencode_dict_add(r, "sig", bencode_string_new((const char *)gsig, 64));
                        bencode_dict_add(r, "v", bencode_string_new((const char *)gv, gvl));
                    }
                } else {
                    size_t nlen = 0; char *nodes = serve_compact_closest(state, tgt, &nlen, 8);
                    bencode_dict_add(r, "nodes", bencode_string_new(nodes ? nodes : "", nlen));
                    free(nodes);
                }
            }
        } else if (qlen == 3 && memcmp(q, "put", 3) == 0) {
            /* BEP-44 put: validate token, then the bounded store verifies sig +
             * target + seq and stores it (no salt/cas in v1). */
            bencode_value_t *kv = bencode_dict_get(a, "k");
            bencode_value_t *sigv = bencode_dict_get(a, "sig");
            bencode_value_t *seqv = bencode_dict_get(a, "seq");
            bencode_value_t *vv = bencode_dict_get(a, "v");
            bencode_value_t *saltv = bencode_dict_get(a, "salt");
            bencode_value_t *tokv = bencode_dict_get(a, "token");
            if (kv && kv->type == BENCODE_STRING && kv->val.str_val.len == 32 &&
                sigv && sigv->type == BENCODE_STRING && sigv->val.str_val.len == 64 &&
                seqv && seqv->type == BENCODE_INT &&
                vv && vv->type == BENCODE_STRING && vv->val.str_val.len > 0 &&
                tokv && tokv->type == BENCODE_STRING &&
                serve_token_valid(state, from_ip, tokv->val.str_val.data, tokv->val.str_val.len)) {
                const unsigned char *k = (const unsigned char *)kv->val.str_val.data;
                const unsigned char *salt = NULL; size_t saltlen = 0;
                if (saltv && saltv->type == BENCODE_STRING && saltv->val.str_val.len <= 64) {
                    salt = (const unsigned char *)saltv->val.str_val.data;
                    saltlen = saltv->val.str_val.len;
                }
                unsigned char tgt[20];
                if (salt) bep44_target_salted(k, salt, saltlen, tgt); else bep44_target(k, tgt);
                dhtstore_put(tgt, k, (uint32_t)seqv->val.int_val,
                             (const unsigned char *)vv->val.str_val.data, vv->val.str_val.len,
                             (const unsigned char *)sigv->val.str_val.data, salt, saltlen, from_ip);
            } else if (!kv && !bencode_dict_get(a, "target") &&
                       vv && vv->type == BENCODE_STRING && vv->val.str_val.len > 0 &&
                       tokv && tokv->type == BENCODE_STRING &&
                       serve_token_valid(state, from_ip, tokv->val.str_val.data, tokv->val.str_val.len)) {
                /* BEP-44 immutable put (`ipub`): no k/sig/target/seq — key it by
                 * SHA1(bencode(v)) ourselves. Content-addressed, self-verifying. */
                dhtstore_put_immutable((const unsigned char *)vv->val.str_val.data,
                                       vv->val.str_val.len, from_ip, NULL);
            }
            /* id-only response */
        }

        bencode_dict_add(resp, "t", bencode_string_new(t_val->val.str_val.data, t_val->val.str_val.len));
        bencode_dict_add(resp, "y", bencode_string_new("r", 1));
        bencode_dict_add(resp, "r", r);
        /* BEP-42: tell the querier the reflexive ip:port we saw it from, so it
         * can learn its own public endpoint (and we learn ours reciprocally). */
        {
            unsigned char ipbuf[6];
            uint16_t bport = htons(from_port);
            memcpy(ipbuf, &from_ip, 4);
            memcpy(ipbuf + 4, &bport, 2);
            bencode_dict_add(resp, "ip", bencode_string_new((const char *)ipbuf, 6));
        }
        
        size_t resp_len;
        char *resp_data = bencode_encode(resp, &resp_len);
        bencode_free(resp);
        bencode_free(msg);
        
        if (resp_data) {
            net_send(state->net, (const uint8_t *)resp_data, resp_len, from_ip, from_port);
            free(resp_data);
        }
        return 0;
    }
    
    bencode_free(msg);
    return 0;
}

void mainline_process_transactions(mainline_state_t *state) {
    if (!state) return;
    
    time_t now = time(NULL);
    int i = 0;
    while (i < state->transaction_count) {
        if (now - state->transactions[i].created > MAINLINE_TRANSACTION_TIMEOUT) {
            for (int j = i; j < state->transaction_count - 1; j++) {
                state->transactions[j] = state->transactions[j + 1];
            }
            state->transaction_count--;
        } else {
            i++;
        }
    }
}

int mainline_get_bootstrap_nodes(mainline_state_t *state, uint32_t *ips, uint16_t *ports, int max_count) {
    if (!state || !ips || !ports || max_count <= 0) return 0;
    
    int count = 0;
    for (int i = 0; i < MAINLINE_BOOTSTRAP_COUNT && count < max_count; i++) {
        uint32_t ip = net_resolve(state->bootstrap_hosts[i]);
        if (ip != 0) {
            ips[count] = ip;
            ports[count] = state->bootstrap_ports[i];
            count++;
        }
    }
    
    return count;
}