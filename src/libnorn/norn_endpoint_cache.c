/* norn_endpoint_cache.c — Endpoint cache with TTL implementation */
#include "norn_endpoint_cache.h"
#include <string.h>
#include <stdlib.h>

void norn_endpoint_cache_init(norn_endpoint_cache_t *cache) {
    if (!cache) return;
    
    memset(cache, 0, sizeof(*cache));
    cache->default_ttl = NORN_ENDPOINT_TTL_DEFAULT;
}

const norn_endpoint_t *norn_endpoint_cache_lookup(norn_endpoint_cache_t *cache,
                                                   const unsigned char *pubkey) {
    if (!cache || !pubkey) return NULL;
    
    time_t now = time(NULL);
    
    /* Linear search (hash table could be faster for large caches) */
    for (int i = 0; i < NORN_ENDPOINT_CACHE_SIZE; i++) {
        if (!cache->entries[i].valid) continue;
        if (cache->entries[i].expires < now) {
            /* Expired, mark as invalid */
            cache->entries[i].valid = 0;
            cache->count--;
            continue;
        }
        if (memcmp(cache->entries[i].pubkey, pubkey, 32) == 0) {
            return &cache->entries[i].endpoint;
        }
    }
    
    return NULL;
}

int norn_endpoint_cache_store(norn_endpoint_cache_t *cache,
                              const unsigned char *pubkey,
                              const norn_endpoint_t *endpoint,
                              int ttl) {
    if (!cache || !pubkey || !endpoint) return -1;
    
    /* Find existing entry or empty slot */
    int slot = -1;
    for (int i = 0; i < NORN_ENDPOINT_CACHE_SIZE; i++) {
        if (!cache->entries[i].valid) {
            if (slot < 0) slot = i;
            continue;
        }
        if (memcmp(cache->entries[i].pubkey, pubkey, 32) == 0) {
            /* Update existing */
            memcpy(&cache->entries[i].endpoint, endpoint, sizeof(*endpoint));
            cache->entries[i].expires = time(NULL) + (ttl > 0 ? ttl : cache->default_ttl);
            return 0;
        }
    }
    
    if (slot < 0) {
        /* Cache full, expire old entries and try again */
        norn_endpoint_cache_expire(cache);
        
        for (int i = 0; i < NORN_ENDPOINT_CACHE_SIZE; i++) {
            if (!cache->entries[i].valid) {
                slot = i;
                break;
            }
        }
        
        if (slot < 0) return -1; /* Still full */
    }
    
    /* Store new entry */
    memcpy(cache->entries[slot].pubkey, pubkey, 32);
    memcpy(&cache->entries[slot].endpoint, endpoint, sizeof(*endpoint));
    cache->entries[slot].expires = time(NULL) + (ttl > 0 ? ttl : cache->default_ttl);
    cache->entries[slot].valid = 1;
    cache->count++;
    
    return 0;
}

int norn_endpoint_cache_remove(norn_endpoint_cache_t *cache,
                               const unsigned char *pubkey) {
    if (!cache || !pubkey) return -1;
    
    for (int i = 0; i < NORN_ENDPOINT_CACHE_SIZE; i++) {
        if (cache->entries[i].valid &&
            memcmp(cache->entries[i].pubkey, pubkey, 32) == 0) {
            cache->entries[i].valid = 0;
            cache->count--;
            return 0;
        }
    }
    
    return -1; /* Not found */
}

int norn_endpoint_cache_expire(norn_endpoint_cache_t *cache) {
    if (!cache) return -1;
    
    time_t now = time(NULL);
    int expired = 0;
    
    for (int i = 0; i < NORN_ENDPOINT_CACHE_SIZE; i++) {
        if (cache->entries[i].valid && cache->entries[i].expires < now) {
            cache->entries[i].valid = 0;
            cache->count--;
            expired++;
        }
    }
    
    return expired;
}