/* SPDX-License-Identifier: MIT */
/* norn_impl.c — Mainline DHT client library implementation.
 * Async non-blocking implementation with transaction queue. */
#include "config.h"
#include "norn.h"
#include "norn_internal.h"
#include "norn_session_internal.h"
#include "norn_transaction.h"
#include "norn_rendezvous.h"
#include "norn_nat.h"
#include "mainline.h"
#include "bep44.h"
#include "crypto.h"
#include "net.h"
#include "dhtstore.h"
#include <sodium.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

/* Forward declarations for private types */
struct dial_context;
typedef struct dial_context dial_context_t;

static void dispatch_response(norn_client_t *client,
                              const uint8_t *data, size_t len,
                              uint32_t from_ip, uint16_t from_port);

norn_client_t *norn_new(const unsigned char *self_pub,
                        const unsigned char *self_sec,
                        const norn_config_t *cfg) {
    if (!self_pub || !self_sec) return NULL;
    
    norn_client_t *client = calloc(1, sizeof(*client));
    if (!client) return NULL;
    
    memcpy(client->self_pub, self_pub, NORN_PUBKEY_BYTES);
    memcpy(client->self_sec, self_sec, NORN_SECRETKEY_BYTES);
    
    if (cfg) client->cfg = *cfg;
    
    /* Compute node ID from public key */
    unsigned char node_id[20];
    bep44_target_for_pubkey(node_id, self_pub);
    
    /* Initialize network: use configured local port (0 = kernel-assigned). */
    uint16_t net_port = (cfg && cfg->local_port) ? cfg->local_port : 0;
    if (net_init(&client->net, net_port) != 0) {
        free(client);
        return NULL;
    }
    
    /* Initialize mainline state */
    if (mainline_init(&client->ml, &client->net, node_id) != 0) {
        net_cleanup(&client->net);
        free(client);
        return NULL;
    }
    
    /* Initialize transaction queue */
    norn_transaction_init(&client->txn);
    
    /* Copy public key to mainline state */
    memcpy(client->ml.self_pub, self_pub, 32);
    client->ml.have_self_pub = 1;
    
    if (cfg && cfg->version) {
        strncpy(client->ml.self_version, cfg->version, sizeof(client->ml.self_version) - 1);
        client->ml.have_self_version = 1;
    }

    /* Protocol (norn) version: major.minor of NORN_VERSION, reported as "pv".
     * Application name: derived from the user-agent (cfg.version) — the part
     * before '/' (e.g. "norn-node" from "norn-node/0.12"). Reported via the
     * BEP-5 "v" field. */
    {
        /* Norn-Version (pv) = major.minor of NORN_VERSION (e.g. "0.12" from
         * "0.12.1"). Take up to the second '.' (or the end if no minor). */
        const char *pv = NORN_VERSION;
        const char *d1 = strchr(pv, '.');
        const char *end = d1 ? strchr(d1 + 1, '.') : NULL;
        size_t pvlen = end ? (size_t)(end - pv) : strlen(pv);
        if (pvlen >= sizeof(client->ml.self_pv)) pvlen = sizeof(client->ml.self_pv) - 1;
        memcpy(client->ml.self_pv, pv, pvlen);
        client->ml.self_pv[pvlen] = '\0';
        const char *ua = (cfg && cfg->version) ? cfg->version : "";
        const char *slash = strchr(ua, '/');
        if (slash && slash > ua) {
            size_t l = (size_t)(slash - ua);
            if (l >= sizeof(client->ml.self_app)) l = sizeof(client->ml.self_app) - 1;
            memcpy(client->ml.self_app, ua, l);
            client->ml.self_app[l] = '\0';
        } else if (ua[0]) {
            /* No slash: treat the whole string as the application name. */
            size_t l = strlen(ua);
            if (l >= sizeof(client->ml.self_app)) l = sizeof(client->ml.self_app) - 1;
            memcpy(client->ml.self_app, ua, l);
            client->ml.self_app[l] = '\0';
        }
        client->ml.have_self_pv = 1;
    }
    
    if (cfg && cfg->read_only) {
        client->ml.read_only = 1;
    }
    
    if (cfg && cfg->private_mode && cfg->boot_count > 0) {
        client->ml.private_mode = 1;
        client->ml.boot_count = cfg->boot_count < 8 ? cfg->boot_count : 8;
        for (int i = 0; i < client->ml.boot_count; i++) {
            client->ml.boot_ips[i] = cfg->boot_ips[i];
            client->ml.boot_ports[i] = cfg->boot_ports[i];
        }
    }
    
    client->initialized = 1;
    client->listen_fd = -1;
    norn_endpoint_cache_init(&client->endpoint_cache);
    norn_rendezvous_init(&client->rv);
    norn_relay_init(&client->relay);
    client->relay.net = &client->net;
    return client;
}

void norn_set_signer(norn_client_t *client, norn_sign_fn fn, void *ud) {
    if (!client) return;
    client->signer = fn;
    client->signer_ud = ud;
}

/* === Application-protocol service registry (FEAT-033) === */

int norn_register_stream_service(norn_client_t *client, norn_service_t service,
                                 void (*cb)(norn_stream_t *stream, void *ud),
                                 void *user_data) {
    if (!client || !cb) return -1;
    /* Replace an existing registration for the same service in place. */
    for (int i = 0; i < client->stream_svc_count; i++) {
        if (client->stream_svcs[i].service == service) {
            client->stream_svcs[i].cb = cb;
            client->stream_svcs[i].ud = user_data;
            return 0;
        }
    }
    if (client->stream_svc_count >= NORN_MAX_SERVICES) return -1;
    int i = client->stream_svc_count++;
    client->stream_svcs[i].service = service;
    client->stream_svcs[i].cb = cb;
    client->stream_svcs[i].ud = user_data;
    return 0;
}

int norn_register_datagram_service(norn_client_t *client, norn_service_t service,
                                   norn_datagram_cb_t cb, void *user_data) {
    if (!client || !cb) return -1;
    for (int i = 0; i < client->dgram_svc_count; i++) {
        if (client->dgram_svcs[i].service == service) {
            client->dgram_svcs[i].cb = cb;
            client->dgram_svcs[i].ud = user_data;
            return 0;
        }
    }
    if (client->dgram_svc_count >= NORN_MAX_SERVICES) return -1;
    int i = client->dgram_svc_count++;
    client->dgram_svcs[i].service = service;
    client->dgram_svcs[i].cb = cb;
    client->dgram_svcs[i].ud = user_data;
    return 0;
}

int norn_client_stream_svc(norn_client_t *client, norn_service_t service,
                           void (**cb)(norn_stream_t *, void *), void **ud) {
    if (!client) return -1;
    for (int i = 0; i < client->stream_svc_count; i++) {
        if (client->stream_svcs[i].service == service) {
            *cb = client->stream_svcs[i].cb;
            *ud = client->stream_svcs[i].ud;
            return 0;
        }
    }
    return -1;
}

int norn_client_dgram_svc(norn_client_t *client, norn_service_t service,
                          norn_datagram_cb_t *cb, void **ud) {
    if (!client) return -1;
    for (int i = 0; i < client->dgram_svc_count; i++) {
        if (client->dgram_svcs[i].service == service) {
            *cb = client->dgram_svcs[i].cb;
            *ud = client->dgram_svcs[i].ud;
            return 0;
        }
    }
    return -1;
}

void norn_free(norn_client_t *client) {
    if (!client) return;
    
    /* Stop Bonjour/mDNS announcement and discovery */
    norn_bonjour_free(client->bonjour);
    client->bonjour = NULL;

    /* Close listener socket */
    if (client->listen_fd >= 0) {
        close(client->listen_fd);
    }
    
    /* Free all sessions */
    for (int i = 0; i < client->session_count; i++) {
        if (client->sessions[i]) {
            norn_session_free(client->sessions[i]);
        }
    }
    free(client->sessions);
    
    /* Cleanup rendezvous service */
    norn_rendezvous_cleanup(&client->rv);
    
    /* Cleanup relay service */
    norn_relay_cleanup(&client->relay);
    
    mainline_cleanup(&client->ml);
    net_cleanup(&client->net);
    free(client);
}

int norn_get_id(const norn_client_t *client, unsigned char out[NORN_ID_BYTES]) {
    if (!client || !out || !client->initialized) return -1;
    memcpy(out, client->ml.self_id, NORN_ID_BYTES);
    return 0;
}

int norn_external_addr(const norn_client_t *client, uint32_t *ip_out,
                       uint16_t *port_out, int *have) {
    if (!client || !ip_out || !port_out || !have) return -1;
    const struct norn_client *c = (const struct norn_client *)client;
    *have = c->have_external_addr;
    if (c->have_external_addr) {
        *ip_out = c->external_ip;
        *port_out = c->external_port;
    }
    return 0;
}

int norn_bootstrap(norn_client_t *client) {
    if (!client || !client->initialized) return -1;
    return mainline_bootstrap(&client->ml);
}

int norn_routing_size(const norn_client_t *client) {
    if (!client) return -1;
    /* norn_client_t is defined in norn_internal.h which casts correctly.
     * mainline_get_node_count doesn't modify the state, so the const cast is safe. */
    const struct norn_client *c = (const struct norn_client *)client;
    return mainline_get_node_count((mainline_state_t *)&c->ml);
}

void norn_crawl(norn_client_t *client) {
    if (!client) return;
    struct norn_client *c = (struct norn_client *)client;
    mainline_crawl_network(&c->ml);
}

int norn_routing_nodes(const norn_client_t *client, norn_routing_node_t *out,
                       int cap) {
    if (!client || !out || cap < 0) return -1;
    if (cap == 0) return 0;
    const struct norn_client *c = (const struct norn_client *)client;
    mainline_state_t *ml = (mainline_state_t *)&c->ml;
    int n = ml->node_count;
    if (n > cap) n = cap;
    for (int i = 0; i < n; i++) {
        memcpy(out[i].id, ml->nodes[i].id, 20);
        out[i].ip = ml->nodes[i].ip;
        out[i].port = ml->nodes[i].port;
        out[i].last_seen = ml->nodes[i].last_seen;
        memcpy(out[i].pv, ml->nodes[i].pv, sizeof(out[i].pv));
        memcpy(out[i].app, ml->nodes[i].app, sizeof(out[i].app));
    }
    return n;
}

int norn_save_dht_nodes(norn_client_t *client, const char *path) {
    if (!client || !path || !client->initialized) return -1;
    return mainline_save_nodes(&client->ml, path);
}

int norn_load_dht_nodes(norn_client_t *client, const char *path) {
    if (!client || !path || !client->initialized) return -1;
    return mainline_load_nodes(&client->ml, path);
}

int norn_save_peer_cache(norn_client_t *client, const char *path) {
    if (!client || !path || !client->initialized) return -1;
    return -1; /* TODO: implement norn_endpoint_cache persistence */
}

int norn_load_peer_cache(norn_client_t *client, const char *path) {
    if (!client || !path || !client->initialized) return -1;
    return -1; /* TODO: implement norn_endpoint_cache persistence */
}

int norn_tick(norn_client_t *client) {
    if (!client || !client->initialized) return -1;
    
    int total_processed = 0;
    
    /* Process pending transactions */
    mainline_process_transactions(&client->ml);
    
    /* Expire old transactions */
    norn_transaction_expire(&client->txn, NORN_TRANSACTION_TIMEOUT);
    
    /* Process sessions (FEAT-016) */
    int session_processed = norn_client_tick_sessions(client);
    if (session_processed > 0) {
        total_processed += session_processed;
    }
    
    /* Non-blocking receive */
    uint8_t buf[2048];
    uint32_t from_ip;
    uint16_t from_port;
    
    fd_set rf;
    struct timeval tv = {0, 0};  /* Zero timeout = non-blocking */
    FD_ZERO(&rf);
    FD_SET(client->net.fd, &rf);
    
    int nfds = select(client->net.fd + 1, &rf, NULL, NULL, &tv);
    if (nfds <= 0) return total_processed;
    
    /* Receive all pending packets */
    while (1) {
        ssize_t len = net_recv(&client->net, buf, sizeof(buf), &from_ip, &from_port);
        if (len <= 0) {
            break;
        }
        
        /* Process packet */
        dispatch_response(client, buf, len, from_ip, from_port);
        total_processed++;
    }
    
    return total_processed;
}

int norn_get_fd(const norn_client_t *client) {
    if (!client || !client->initialized) return -1;
    return client->net.fd;
}

int norn_put_mutable(norn_client_t *client,
                     const unsigned char *pubkey, const unsigned char *secret,
                     const unsigned char *value, size_t value_len,
                     uint32_t seq) {
    return norn_put_mutable_salt(client, pubkey, secret, value, value_len, seq,
                                 NULL, 0);
}

int norn_put_mutable_salt(norn_client_t *client,
                          const unsigned char *pubkey, const unsigned char *secret,
                          const unsigned char *value, size_t value_len,
                          uint32_t seq, const unsigned char *salt, size_t saltlen) {
    if (!client || !client->initialized || !pubkey || !secret || !value) return -1;
    if (value_len > 1000) return -1;

    /* Compute target: salted if a salt is given, else the unsalted form. */
    unsigned char target[20];
    if (salt && saltlen)
        bep44_target_salted(pubkey, salt, saltlen, target);
    else
        bep44_target_for_pubkey(target, pubkey);

    /* Sign value */
    unsigned char sig[64];
    unsigned char signbuf[2048];
    int signlen = bep44_signbuf(seq, value, value_len, signbuf, sizeof(signbuf));
    if (signlen < 0) return -1;

    if (crypto_sign_detached(sig, NULL, signbuf, signlen, secret) != 0) return -1;

    /* Issue async put */
    return mainline_lookup_mutable(&client->ml, target, 1, pubkey, seq,
                                    value, value_len, sig, salt, saltlen, 0,
                                    NULL, NULL, 0, 0, NULL);
}

int norn_get_mutable(norn_client_t *client,
                     const unsigned char *pubkey,
                     norn_get_callback_t callback, void *user_data) {
    return norn_get_mutable_salt(client, pubkey, NULL, 0, callback, user_data);
}

int norn_get_mutable_salt(norn_client_t *client,
                          const unsigned char *pubkey,
                          const unsigned char *salt, size_t saltlen,
                          norn_get_callback_t callback, void *user_data) {
    if (!client || !client->initialized || !pubkey || !callback) return -1;

    /* Create transaction */
    norn_transaction_t *txn = norn_transaction_new(&client->txn, TXN_GET_MUTABLE, pubkey);
    if (!txn) return -1;

    txn->get_callback = callback;
    txn->user_data = user_data;

    /* Compute target: salted if a salt is given, else the unsalted form. */
    unsigned char target[20];
    if (salt && saltlen)
        bep44_target_salted(pubkey, salt, saltlen, target);
    else
        bep44_target_for_pubkey(target, pubkey);

    /* Issue async get */
    return mainline_lookup_mutable(&client->ml, target, 0, NULL, 0,
                                    NULL, 0, NULL, salt, saltlen, 0,
                                    NULL, NULL, 0, 0, NULL);
}

int norn_put_immutable(norn_client_t *client,
                       const unsigned char *value, size_t value_len) {
    if (!client || !client->initialized || !value) return -1;
    if (value_len > 1000) return -1;
    
    unsigned char target[20];
    if (bep44_immutable_target(value, value_len, target) != 0) return -1;
    
    return mainline_lookup_mutable(&client->ml, target, 1, NULL, 0,
                                    value, value_len, NULL, NULL, 0, 1,
                                    NULL, NULL, 0, 0, NULL);
}

int norn_get_immutable(norn_client_t *client,
                       const unsigned char *key,
                       norn_get_callback_t callback, void *user_data) {
    if (!client || !client->initialized || !key || !callback) return -1;
    
    /* Create transaction */
    norn_transaction_t *txn = norn_transaction_new(&client->txn, TXN_GET_IMMUTABLE, key);
    if (!txn) return -1;
    
    txn->get_callback = callback;
    txn->user_data = user_data;
    
    return mainline_lookup_mutable(&client->ml, key, 0, NULL, 0,
                                    NULL, 0, NULL, NULL, 0, 1,
                                    NULL, NULL, 0, 0, NULL);
}

int norn_announce(norn_client_t *client,
                  const unsigned char *info_hash) {
    if (!client || !client->initialized || !info_hash) return -1;
    
    return mainline_lookup(&client->ml, info_hash, 1, client->net.port,
                          NULL, NULL, 0, NULL);
}

int norn_discover(norn_client_t *client,
                  const unsigned char *info_hash,
                  norn_peer_callback_t callback, void *user_data) {
    if (!client || !client->initialized || !info_hash || !callback) return -1;
    
    /* Create transaction */
    norn_transaction_t *txn = norn_transaction_new(&client->txn, TXN_DISCOVER, info_hash);
    if (!txn) return -1;
    
    txn->peer_callback = callback;
    txn->user_data = user_data;
    
    return 0;
}

int norn_resolve_node(norn_client_t *client, const unsigned char *node_id,
                      uint32_t *ip_out, uint16_t *port_out,
                      unsigned char *pubkey_out, int timeout_ms) {
    if (!client || !client->initialized || !node_id || !ip_out || !port_out)
        return -1;
    return mainline_lookup_ex(&client->ml, node_id, 0, 0,
                              ip_out, port_out, pubkey_out, timeout_ms, NULL);
}

int norn_routing_lookup(const norn_client_t *client, const unsigned char *node_id,
                        uint32_t *ip_out, uint16_t *port_out) {
    if (!client || !node_id || !ip_out || !port_out) return -1;
    const struct norn_client *c = (const struct norn_client *)client;
    for (int i = 0; i < c->ml.node_count; i++) {
        if (memcmp(c->ml.nodes[i].id, node_id, 20) == 0) {
            *ip_out = c->ml.nodes[i].ip;
            *port_out = ntohs(c->ml.nodes[i].port);
            return 1;
        }
    }
    return 0;
}

int norn_routing_pubkey(const norn_client_t *client, const unsigned char *node_id,
                        unsigned char *pubkey_out) {
    if (!client || !node_id || !pubkey_out) return -1;
    const struct norn_client *c = (const struct norn_client *)client;
    for (int i = 0; i < c->ml.node_count; i++) {
        if (memcmp(c->ml.nodes[i].id, node_id, 20) == 0) {
            if (!c->ml.nodes[i].have_pubkey) return 0;
            memcpy(pubkey_out, c->ml.nodes[i].pubkey, 32);
            return 1;
        }
    }
    return 0;
}

int norn_dht_list(int want_immutable, norn_dht_item_t *out, int max) {
    if (!out || max <= 0) return -1;
    /* dhtstore_list returns dht_item_info_t with the same field layout as
     * norn_dht_item_t; copy field-by-field to keep the public type opaque. */
    dht_item_info_t info[128];
    int n = dhtstore_list(want_immutable, info, max < 128 ? max : 128);
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        memcpy(out[i].target, info[i].target, 20);
        out[i].immutable = info[i].immutable;
        out[i].vlen = info[i].vlen;
        out[i].seq = info[i].seq;
        out[i].stored = info[i].stored;
    }
    return n;
}

int norn_dht_get_value(const unsigned char *target, unsigned char *out, size_t cap) {
    if (!target || !out || cap == 0) return -1;
    unsigned char k[32], sig[64], v[1024];
    uint32_t seq;
    size_t vlen;
    int got = dhtstore_get_ex(target, k, &seq, v, sizeof(v) < cap ? sizeof(v) : cap,
                              &vlen, sig, NULL);
    if (got < 0) return -1;
    memcpy(out, v, vlen);
    return (int)vlen;
}

int norn_dht_get_full(const unsigned char *target, unsigned char *pubkey_out,
                      uint32_t *seq_out, unsigned char *val_out, size_t vcap,
                      size_t *vlen_out, unsigned char *sig_out, int *immutable_out) {
    if (!target) return -1;
    return dhtstore_get_ex(target, pubkey_out ? pubkey_out : (unsigned char[32]){0},
                           seq_out, val_out, vcap, vlen_out, sig_out, immutable_out);
}

int norn_dht_restore_mutable(const unsigned char *target, const unsigned char *pubkey,
                             uint32_t seq, const unsigned char *value, size_t vlen,
                             const unsigned char *sig) {
    if (!target || !pubkey || !value || !sig) return -1;
    return dhtstore_put(target, pubkey, seq, value, vlen, sig, NULL, 0, 0);
}

int norn_dht_restore_immutable(const unsigned char *value, size_t vlen) {
    if (!value || vlen == 0) return -1;
    return dhtstore_put_immutable(value, vlen, 0, NULL);
}

int norn_dht_del(const unsigned char *target) {
    if (!target) return -1;
    return dhtstore_del(target);
}

int norn_dht_get_mutable(norn_client_t *client,
                          const unsigned char *target,
                          const unsigned char *pubkey,
                          const unsigned char *salt, size_t saltlen,
                          unsigned char *value_out, size_t *vlen_out, size_t vcap,
                          int timeout_ms) {
    if (!client || !client->initialized || !target || !pubkey || !value_out || !vlen_out) return -1;
    if (vcap == 0) return -1;
    return mainline_lookup_mutable(&client->ml, target, 0, pubkey, 0,
                                    NULL, 0, NULL, salt, saltlen, 0,
                                    value_out, vlen_out, vcap,
                                    timeout_ms, NULL);
}

int norn_dht_get_immutable(norn_client_t *client,
                            const unsigned char *key,
                            unsigned char *value_out, size_t *vlen_out, size_t vcap,
                            int timeout_ms) {
    if (!client || !client->initialized || !key || !value_out || !vlen_out) return -1;
    if (vcap == 0) return -1;
    return mainline_lookup_mutable(&client->ml, key, 0, NULL, 0,
                                    NULL, 0, NULL, NULL, 0, 1,
                                    value_out, vlen_out, vcap,
                                    timeout_ms, NULL);
}

int norn_encode_mutable(const norn_mutable_t *rec,
                         unsigned char *out, size_t outcap) {
    if (!rec || !out) return -1;
    return bep44_encode(out, outcap, rec->pubkey,
                        rec->value, rec->value_len, rec->seq,
                        rec->have_sig ? rec->sig : NULL);
}

int norn_decode_mutable(const unsigned char *buf, size_t len,
                        norn_mutable_t *rec) {
    if (!buf || !rec || len < 32 + 4 + 2 + 64) return -1;
    
    memset(rec, 0, sizeof(*rec));
    
    unsigned char *value;
    size_t vlen;
    uint32_t seq;
    unsigned char sig[64];
    
    int ret = bep44_decode(buf, len, rec->pubkey, &value, &vlen, &seq, sig);
    if (ret != 0) return -1;
    
    if (vlen > sizeof(rec->value)) return -1;
    
    memcpy(rec->value, value, vlen);
    rec->value_len = vlen;
    rec->seq = seq;
    memcpy(rec->sig, sig, 64);
    rec->have_sig = 1;
    
    return 0;
}

/* Dispatch response to appropriate callback */
static void dispatch_response(norn_client_t *client,
                              const uint8_t *data, size_t len,
                              uint32_t from_ip, uint16_t from_port) {
    if (len < 1) return;

    /* RelayForward must be unwrapped before session dispatch so that relay
     * sessions (which store relay_ip/port as peer) receive the inner payload
     * rather than the wrapper. */
    if (data[0] == NORN_MSG_RELAY_FORWARD) {
        norn_relay_forward_t fwd;
        if (norn_decode_relay_forward(&fwd, data, len) == 0) {
            /* Acting as relay: forward to the other endpoint. */
            if (client->relay.enabled) {
                norn_relay_handle_forward(&client->relay, &fwd, from_ip, htons(from_port));
            }
            /* Acting as relay session endpoint: find session by relay_session_id
             * and feed inner payload to the session state machine. */
            for (int i = 0; i < client->session_count; i++) {
                norn_session_t *s = client->sessions[i];
                if (s->relay_enabled &&
                    memcmp(s->relay_session_id, fwd.session_id,
                           NORN_RELAY_SESSION_ID_LEN) == 0) {
                    norn_session_process_packet(s, fwd.payload, fwd.payload_len,
                                               from_ip, htons(from_port));
                    break;
                }
            }
        }
        return;
    }

    /* Try the session layer first: this matches packets from known session
     * peers (established encrypted traffic) and new INIT packets ("DHCH").
     * Returns 0 if handled, -1 to fall through to DHT/binary-protocol. */
    if (norn_session_dispatch_udp(client, data, len, from_ip, htons(from_port)) == 0)
        return;

    /* Route binary protocol messages */
    if (data[0] >= 0x10 && data[0] <= 0x2F) {
        /* NAT traversal messages (0x10-0x1F) */
        if (data[0] == NORN_MSG_HOLEPUNCH_REQ || data[0] == NORN_MSG_HOLEPUNCH_RESP) {
            norn_holepunch_req_t req;
            norn_holepunch_resp_t resp;
            
            if (data[0] == NORN_MSG_HOLEPUNCH_REQ && len >= NORN_HOLEPUNCH_REQ_LEN) {
                if (norn_decode_holepunch_req(&req, data, len) == 0) {
                    norn_holepunch_resp_t resp_out;
                    int result = norn_rendezvous_handle_req(&client->rv, &req, from_ip, from_port, client, &resp_out);
                    if (result == 1) {
                        uint8_t resp_buf[NORN_HOLEPUNCH_RESP_LEN];
                        if (norn_encode_holepunch_resp(&resp_out, resp_buf) == 0) {
                            net_send(&client->net, resp_buf, NORN_HOLEPUNCH_RESP_LEN, from_ip, from_port);
                        }
                    }
                }
            } else if (data[0] == NORN_MSG_HOLEPUNCH_RESP && len >= NORN_HOLEPUNCH_RESP_LEN) {
                if (norn_decode_holepunch_resp(&resp, data, len) == 0) {
                    /* FEAT-023: Handle hole punch response callback */
                    for (int i = 0; i < client->holepunch_pending_count; i++) {
                        if (client->holepunch_pending[i].active &&
                            memcmp(client->holepunch_pending[i].ephemeral_pubkey, 
                                   resp.peer_ephemeral_pubkey, 32) == 0) {
                            /* Found matching pending request */
                            if (client->holepunch_pending[i].callback) {
                                client->holepunch_pending[i].callback(client, &resp,
                                                                       client->holepunch_pending[i].user_data);
                            }
                            client->holepunch_pending[i].active = 0;
                            break;
                        }
                    }
                }
            }
            return;
        }
        
        /* FEAT-023: Probe detection */
        if (data[0] == NORN_MSG_PROBE && len >= NORN_PROBE_LEN) {
            norn_probe_t probe;
            if (norn_decode_probe(&probe, data, len) == 0) {
                /* Check if this matches a pending hole punch */
                for (int i = 0; i < client->holepunch_pending_count; i++) {
                    if (client->holepunch_pending[i].active &&
                        memcmp(client->holepunch_pending[i].ephemeral_pubkey,
                               probe.ephemeral_pubkey, 32) == 0) {
                        /* FEAT-023: Probe received from peer!
                         * Create session and notify dial callback
                         */
                        
                        /* Get dial context from pending request */
                        void *dial_ctx = client->holepunch_pending[i].user_data;
                        
                        /* Mark as inactive */
                        client->holepunch_pending[i].active = 0;
                        
                        /* Create session using helper */
                        norn_session_from_probe(client, dial_ctx, from_ip, from_port);
                        
                        break;
                    }
                }
            }
            return;
        }
        
        /* Relay messages (0x20-0x2F) — RelayForward (0x21) handled above. */
        if (data[0] >= NORN_MSG_RELAY_CREATE && data[0] <= NORN_MSG_RELAY_CLOSE) {
            if (data[0] == NORN_MSG_RELAY_CREATE && len >= NORN_RELAY_CREATE_LEN) {
                norn_relay_create_t req;
                if (norn_decode_relay_create(&req, data, len) == 0) {
                    /* Are we the intended target? Send RelayAccept back. */
                    if (memcmp(req.target_pubkey, client->self_pub, 32) == 0) {
                        norn_relay_accept_t acc;
                        acc.msg_type = NORN_MSG_RELAY_ACCEPT;
                        memcpy(acc.session_id, req.session_id, NORN_RELAY_SESSION_ID_LEN);
                        memset(acc.initiator_pubkey, 0, 32);
                        bf_sign(acc.signature, (const unsigned char *)&acc,
                                offsetof(norn_relay_accept_t, signature), client->self_sec);
                        uint8_t acc_buf[NORN_RELAY_ACCEPT_LEN];
                        if (norn_encode_relay_accept(&acc, acc_buf) == 0)
                            net_send(&client->net, acc_buf, sizeof(acc_buf),
                                     from_ip, htons(from_port));
                    } else if (client->relay.enabled) {
                        /* Acting as relay: store session and forward to target. */
                        uint8_t session_id[NORN_RELAY_SESSION_ID_LEN];
                        if (norn_relay_handle_create(&client->relay, &req, from_ip,
                                                      htons(from_port), session_id) == 0) {
                            const norn_endpoint_t *tgt = norn_endpoint_cache_lookup(
                                &client->endpoint_cache, req.target_pubkey);
                            if (tgt && tgt->ip != 0) {
                                uint8_t fwd[NORN_RELAY_CREATE_LEN];
                                norn_relay_create_t fwd_req = req;
                                if (norn_encode_relay_create(&fwd_req, fwd) == 0)
                                    net_send(&client->net, fwd, sizeof(fwd),
                                             tgt->ip, tgt->port);
                            }
                        }
                    }
                }
            } else if (data[0] == NORN_MSG_RELAY_ACCEPT && len >= NORN_RELAY_ACCEPT_LEN) {
                norn_relay_accept_t accept;
                if (norn_decode_relay_accept(&accept, data, len) == 0) {
                    /* Acting as relay: mark session active and forward to initiator. */
                    norn_relay_session_t *rs = norn_relay_find_session(
                        &client->relay, accept.session_id);
                    if (rs) {
                        rs->active = 1;
                        rs->target_ip = from_ip;
                        rs->target_port = htons(from_port);
                        uint8_t fwd[NORN_RELAY_ACCEPT_LEN];
                        if (norn_encode_relay_accept(&accept, fwd) == 0)
                            net_send(&client->net, fwd, sizeof(fwd),
                                     rs->initiator_ip, rs->initiator_port);
                    }
                    /* Acting as initiator: relay accept received — start handshake. */
                    for (int i = 0; i < 8; i++) {
                        if (!client->relay_pending[i].active) continue;
                        if (memcmp(client->relay_pending[i].session_id,
                                   accept.session_id, NORN_RELAY_SESSION_ID_LEN) != 0) continue;
                        norn_session_t *s = norn_session_new(
                            client, client->relay_pending[i].suite);
                        if (!s) { client->relay_pending[i].active = 0; break; }
                        s->is_initiator = 1;
                        norn_session_set_identity(s, client->self_pub, client->self_sec);
                        norn_session_set_signer(s, client->signer, client->signer_ud);
                        memcpy(s->peer_pubkey, client->relay_pending[i].peer_pubkey, 32);
                        s->relay_enabled = 1;
                        memcpy(s->relay_session_id, accept.session_id,
                               NORN_RELAY_SESSION_ID_LEN);
                        s->relay_ip = client->relay_pending[i].relay_ip;
                        s->relay_port = client->relay_pending[i].relay_port;
                        s->fd = -1;
                        if (channel_gen_ephemeral(&s->channel) != 0 ||
                            norn_client_add_session(client, s) != 0) {
                            free(s->streams); free(s);
                            client->relay_pending[i].active = 0;
                            break;
                        }
                        s->callback = client->relay_pending[i].callback;
                        s->user_data = client->relay_pending[i].user_data;
                        client->relay_pending[i].active = 0;
                        norn_session_send_pending(s);
                        break;
                    }
                }
            } else if (data[0] == NORN_MSG_RELAY_CLOSE) {
                if (len >= 1 + NORN_RELAY_SESSION_ID_LEN) {
                    const uint8_t *sid = data + 1;
                    norn_relay_close_session(&client->relay, sid);
                    /* Also close any relay session on the initiator side. */
                    for (int i = 0; i < client->session_count; i++) {
                        norn_session_t *s = client->sessions[i];
                        if (s->relay_enabled &&
                            memcmp(s->relay_session_id, sid,
                                   NORN_RELAY_SESSION_ID_LEN) == 0) {
                            s->state = NORN_SESSION_CLOSED;
                            if (s->callback)
                                s->callback(s, NORN_SESSION_CLOSED, s->user_data);
                            break;
                        }
                    }
                }
            }
            return;
        }
    }
    
    /* Process through mainline state machine */
    mainline_process_packet(&client->ml, data, len, from_ip, from_port);
    
    /* Note: Callbacks are invoked by mainline_process_packet when
     * a matching transaction is found. The transaction callback
     * mechanism is handled by mainline_lookup callbacks.
     *
     * For norn-level callbacks, we would need to integrate with
     * mainline's transaction system or add our own response parser.
     * This is deferred pending mainline async refactor (FEAT-012 phase 2).
     */
}