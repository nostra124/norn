/* SPDX-License-Identifier: MIT */
#include "replaycache.h"
#include <string.h>

void replaycache_init(replaycache_t *rc, long window_secs) {
    if (!rc) return;
    memset(rc, 0, sizeof(*rc));
    rc->window = window_secs;
}

int replaycache_seen(replaycache_t *rc, const unsigned char nonce[16], long now) {
    if (!rc || !nonce) return 0;
    /* a match within the window is a replay */
    for (int i = 0; i < REPLAY_SLOTS; i++) {
        replay_slot_t *s = &rc->slots[i];
        if (s->t != 0 && (now - s->t) < rc->window &&
            memcmp(s->nonce, nonce, 16) == 0)
            return 1;
    }
    /* fresh: record it, overwriting the oldest ring slot */
    memcpy(rc->slots[rc->next].nonce, nonce, 16);
    rc->slots[rc->next].t = now;
    rc->next = (rc->next + 1) % REPLAY_SLOTS;
    return 0;
}
