/* BUG-118: receiver-side nonce anti-replay cache. */
#include "replaycache.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void mk(unsigned char n[16], int seed) { memset(n, 0, 16); n[0] = (unsigned char)seed; n[15] = (unsigned char)(seed >> 8); }

int main(void) {
    replaycache_t rc;
    replaycache_init(&rc, 120);   /* 120s window */

    unsigned char a[16], b[16];
    mk(a, 1); mk(b, 2);

    /* first sight of a nonce is fresh; an immediate repeat is a replay */
    assert(replaycache_seen(&rc, a, 1000) == 0);
    assert(replaycache_seen(&rc, a, 1000) == 1);
    assert(replaycache_seen(&rc, a, 1050) == 1);   /* still within the window */

    /* a different nonce is independent */
    assert(replaycache_seen(&rc, b, 1000) == 0);
    assert(replaycache_seen(&rc, b, 1000) == 1);

    /* once the window passes, the same nonce is accepted again (and re-recorded) */
    assert(replaycache_seen(&rc, a, 1000 + 120) == 0);
    assert(replaycache_seen(&rc, a, 1000 + 120) == 1);

    /* ring eviction: flood with > REPLAY_SLOTS distinct nonces, then an early one
     * is no longer remembered (its slot was overwritten) → treated as fresh. */
    replaycache_init(&rc, 120);
    unsigned char first[16]; mk(first, 7);
    assert(replaycache_seen(&rc, first, 2000) == 0);
    for (int i = 0; i < REPLAY_SLOTS + 10; i++) {
        unsigned char n[16]; mk(n, 1000 + i);
        replaycache_seen(&rc, n, 2000);
    }
    assert(replaycache_seen(&rc, first, 2000) == 0);   /* evicted → not flagged */

    /* argument guards: NULL cache / NULL nonce are no-ops, never a false replay */
    replaycache_init(NULL, 60);                        /* no crash */
    assert(replaycache_seen(NULL, a, 1000) == 0);      /* NULL cache → not seen */
    assert(replaycache_seen(&rc, NULL, 1000) == 0);    /* NULL nonce → not seen */

    printf("test_replaycache: OK — replay caught, window expiry, ring eviction, guards\n");
    return 0;
}
