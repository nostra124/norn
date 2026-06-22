/* norn_impl.c — Mainline DHT client library implementation.
 * Async non-blocking implementation with transaction queue. */
#include "norn.h"
#include "norn_transaction.h"
#include "mainline.h"
#include "bep44.h"
#include "crypto.h"
#include "net.h"
#include <sodium.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

struct norn_client {
    net_t net;
    mainline_state_t ml;
    norn_transaction_state_t txn;
    unsigned char self_pub[NORN_PUBKEY_BYTES];
    unsigned char self_sec[NORN_SECRETKEY_BYTES];
    norn_config_t cfg;
    int initialized;
};

/* Forward declarations */
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
    
    /* Initialize network */
    if (net_init(&client->net, 0) != 0) {
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
    return client;
}

void norn_free(norn_client_t *client) {
    if (!client) return;
    
    mainline_cleanup(&client->ml);
    net_cleanup(&client->net);
    free(client);
}

int norn_get_id(const norn_client_t *client, unsigned char out[NORN_ID_BYTES]) {
    if (!client || !out || !client->initialized) return -1;
    memcpy(out, client->ml.self_id, NORN_ID_BYTES);
    return 0;
}

int norn_bootstrap(norn_client_t *client) {
    if (!client || !client->initialized) return -1;
    return mainline_bootstrap(&client->ml);
}

int norn_tick(norn_client_t *client) {
    if (!client || !client->initialized) return -1;
    
    /* Process pending transactions */
    mainline_process_transactions(&client->ml);
    
    /* Expire old transactions */
    norn_transaction_expire(&client->txn, NORN_TRANSACTION_TIMEOUT);
    
    /* Non-blocking receive */
    uint8_t buf[2048];
    uint32_t from_ip;
    uint16_t from_port;
    
    fd_set rf;
    struct timeval tv = {0, 0};  /* Zero timeout = non-blocking */
    FD_ZERO(&rf);
    FD_SET(client->net.fd, &rf);
    
    int nfds = select(client->net.fd + 1, &rf, NULL, NULL, &tv);
    if (nfds <= 0) return 0;
    
    /* Receive all pending packets */
    int processed = 0;
    while (1) {
        ssize_t len = net_recv(&client->net, buf, sizeof(buf), &from_ip, &from_port);
        if (len <= 0) {
            break;
        }
        
        /* Process packet */
        dispatch_response(client, buf, len, from_ip, from_port);
        processed++;
    }
    
    return processed;
}

int norn_get_fd(const norn_client_t *client) {
    if (!client || !client->initialized) return -1;
    return client->net.fd;
}

int norn_put_mutable(norn_client_t *client,
                     const unsigned char *pubkey, const unsigned char *secret,
                     const unsigned char *value, size_t value_len,
                     uint32_t seq) {
    if (!client || !client->initialized || !pubkey || !secret || !value) return -1;
    if (value_len > 1000) return -1;
    
    /* Compute target */
    unsigned char target[20];
    bep44_target_for_pubkey(target, pubkey);
    
    /* Sign value */
    unsigned char sig[64];
    unsigned char signbuf[2048];
    int signlen = bep44_signbuf(seq, value, value_len, signbuf, sizeof(signbuf));
    if (signlen < 0) return -1;
    
    if (crypto_sign_detached(sig, NULL, signbuf, signlen, secret) != 0) return -1;
    
    /* Issue async put */
    return mainline_lookup_mutable(&client->ml, target, 1, pubkey, seq,
                                    value, value_len, sig, NULL, 0, 0,
                                    NULL, NULL, 0, 0, NULL);
}

int norn_get_mutable(norn_client_t *client,
                     const unsigned char *pubkey,
                     norn_get_callback_t callback, void *user_data) {
    if (!client || !client->initialized || !pubkey || !callback) return -1;
    
    /* Create transaction */
    norn_transaction_t *txn = norn_transaction_new(&client->txn, TXN_GET_MUTABLE, pubkey);
    if (!txn) return -1;
    
    txn->get_callback = callback;
    txn->user_data = user_data;
    
    /* Compute target */
    unsigned char target[20];
    bep44_target_for_pubkey(target, pubkey);
    
    /* Issue async get */
    return mainline_lookup_mutable(&client->ml, target, 0, NULL, 0,
                                    NULL, 0, NULL, NULL, 0, 0,
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
    
    return mainline_lookup(&client->ml, info_hash, 0, 0,
                          NULL, NULL, 0, NULL);
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