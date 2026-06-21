/* norn.c — Mainline DHT client library implementation.
 * Wraps the mainline DHT implementation in a clean API. */
#include "norn.h"
#include "mainline.h"
#include "bep44.h"
#include "crypto.h"
#include <string.h>
#include <stdlib.h>

struct norn_client {
    mainline_state_t *ml;
    unsigned char self_pub[NORN_PUBKEY_BYTES];
    unsigned char self_sec[NORN_SECRETKEY_BYTES];
    norn_config_t cfg;
};

norn_client_t *norn_new(const unsigned char *self_pub,
                        const unsigned char *self_sec,
                        const norn_config_t *cfg) {
    if (!self_pub || !self_sec) return NULL;
    
    norn_client_t *client = calloc(1, sizeof(*client));
    if (!client) return NULL;
    
    memcpy(client->self_pub, self_pub, NORN_PUBKEY_BYTES);
    memcpy(client->self_sec, self_sec, NORN_SECRETKEY_BYTES);
    
    if (cfg) client->cfg = *cfg;
    else memset(&client->cfg, 0, sizeof(client->cfg));
    
    /* Allocate mainline state */
    client->ml = calloc(1, sizeof(*client->ml));
    if (!client->ml) {
        free(client);
        return NULL;
    }
    
    /* Initialize mainline state */
    memcpy(client->ml->self_pub, self_pub, 32);
    client->ml->have_self_pub = 1;
    
    if (cfg && cfg->version) {
        strncpy(client->ml->self_version, cfg->version, sizeof(client->ml->self_version) - 1);
        client->ml->have_self_version = 1;
    }
    
    if (cfg && cfg->read_only) {
        client->ml->read_only = 1;
    }
    
    if (cfg && cfg->private_mode && cfg->boot_count > 0) {
        client->ml->private_mode = 1;
        client->ml->boot_count = cfg->boot_count < 8 ? cfg->boot_count : 8;
        for (int i = 0; i < client->ml->boot_count; i++) {
            client->ml->boot_ips[i] = cfg->boot_ips[i];
            client->ml->boot_ports[i] = cfg->boot_ports[i];
        }
    }
    
    return client;
}

void norn_free(norn_client_t *client) {
    if (!client) return;
    if (client->ml) {
        if (client->ml->net) {
            /* net_t cleanup handled by caller */
        }
        free(client->ml);
    }
    free(client);
}

int norn_get_id(const norn_client_t *client, unsigned char out[NORN_ID_BYTES]) {
    if (!client || !client->ml || !out) return -1;
    memcpy(out, client->ml->self_id, NORN_ID_BYTES);
    return 0;
}

int norn_bootstrap(norn_client_t *client) {
    if (!client || !client->ml) return -1;
    /* Bootstrap would contact DHT nodes */
    /* For now, return success if boot peers are configured */
    return (client->ml->boot_count > 0 || !client->ml->private_mode) ? 0 : -1;
}

int norn_put_mutable(norn_client_t *client,
                     const unsigned char *pubkey, const unsigned char *secret,
                     const unsigned char *value, size_t value_len,
                     uint32_t seq) {
    if (!client || !client->ml || !pubkey || !secret || !value) return -1;
    if (value_len > 1000) return -1;  /* BEP-44 limit */
    
    /* Create BEP-44 mutable record and store */
    /* This would send put request to DHT */
    (void)seq;  /* TODO: implement */
    return 0;
}

int norn_get_mutable(norn_client_t *client,
                     const unsigned char *pubkey,
                     norn_get_callback_t callback, void *user_data) {
    if (!client || !client->ml || !pubkey || !callback) return -1;
    (void)user_data;  /* TODO: implement */
    /* This would send get request to DHT and invoke callback */
    return 0;
}

int norn_put_immutable(norn_client_t *client,
                       const unsigned char *value, size_t value_len) {
    if (!client || !client->ml || !value) return -1;
    (void)value_len;  /* TODO: implement */
    return 0;
}

int norn_get_immutable(norn_client_t *client,
                       const unsigned char *key,
                       norn_get_callback_t callback, void *user_data) {
    if (!client || !client->ml || !key || !callback) return -1;
    (void)user_data;  /* TODO: implement */
    return 0;
}

int norn_announce(norn_client_t *client,
                  const unsigned char *info_hash) {
    if (!client || !client->ml || !info_hash) return -1;
    return 0;
}

int norn_discover(norn_client_t *client,
                  const unsigned char *info_hash,
                  norn_peer_callback_t callback, void *user_data) {
    if (!client || !client->ml || !info_hash || !callback) return -1;
    (void)user_data;  /* TODO: implement */
    return 0;
}

int norn_tick(norn_client_t *client) {
    if (!client || !client->ml) return 0;
    /* Process pending transactions */
    return 0;
}

int norn_get_fd(const norn_client_t *client) {
    if (!client || !client->ml || !client->ml->net) return -1;
    return client->ml->net->fd;
}

int norn_encode_mutable(const norn_mutable_t *rec,
                        unsigned char *out, size_t outcap) {
    if (!rec || !out) return -1;
    /* Encode using BEP-44 format */
    return bep44_encode(out, outcap, rec->pubkey, 
                        rec->value, rec->value_len, rec->seq, 
                        rec->have_sig ? rec->sig : NULL);
}

int norn_decode_mutable(const unsigned char *buf, size_t len,
                        norn_mutable_t *rec) {
    if (!buf || !rec) return -1;
    /* Decode BEP-44 format */
    unsigned char *value;
    size_t vlen;
    uint32_t seq;
    unsigned char sig[64];
    
    int ret = bep44_decode(buf, len, rec->pubkey, &value, &vlen, &seq, sig);
    if (ret != 0) return -1;
    
    /* Copy value into the record buffer */
    if (vlen > sizeof(rec->value)) return -1;
    memcpy(rec->value, value, vlen);
    rec->value_len = vlen;
    rec->seq = seq;
    memcpy(rec->sig, sig, 64);
    rec->have_sig = 1;
    
    return 0;
}