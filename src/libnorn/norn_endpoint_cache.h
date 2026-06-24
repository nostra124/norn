/* norn_endpoint_cache.h — Endpoint cache with TTL
 * 
 * Caches resolved endpoints to avoid repeated DHT lookups.
 * Each entry has a TTL (time-to-live) and is expired after.
 * Thread-unsafe: caller must synchronize.
 */
#ifndef NORN_ENDPOINT_CACHE_H
#define NORN_ENDPOINT_CACHE_H

#include "norn_session.h"
#include <time.h>

#define NORN_ENDPOINT_CACHE_SIZE 256
#define NORN_ENDPOINT_TTL_DEFAULT 300 /* 5 minutes */

typedef struct {
    unsigned char pubkey[32];
    norn_endpoint_t endpoint;
    time_t expires;
    int valid;
} norn_cached_endpoint_t;

typedef struct {
    norn_cached_endpoint_t entries[NORN_ENDPOINT_CACHE_SIZE];
    int count;
    int default_ttl;
} norn_endpoint_cache_t;

/* Initialize cache */
void norn_endpoint_cache_init(norn_endpoint_cache_t *cache);

/* Lookup endpoint by pubkey (returns NULL if not found or expired) */
const norn_endpoint_t *norn_endpoint_cache_lookup(norn_endpoint_cache_t *cache,
                                                   const unsigned char *pubkey);

/* Store endpoint in cache */
int norn_endpoint_cache_store(norn_endpoint_cache_t *cache,
                              const unsigned char *pubkey,
                              const norn_endpoint_t *endpoint,
                              int ttl);

/* Remove endpoint from cache */
int norn_endpoint_cache_remove(norn_endpoint_cache_t *cache,
                               const unsigned char *pubkey);

/* Expire old entries */
int norn_endpoint_cache_expire(norn_endpoint_cache_t *cache);

#endif /* NORN_ENDPOINT_CACHE_H */