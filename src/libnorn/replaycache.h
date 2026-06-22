#ifndef REPLAYCACHE_H
#define REPLAYCACHE_H

/* Receiver-side anti-replay for signed idexch packets (BUG-118). Each sender uses
 * a fresh random 16-byte nonce per packet, so a replayed packet carries a nonce we
 * have already seen. We remember recently-seen nonces for a time window and reject
 * a repeat — no wire-format change, so old/new daemons interoperate during rollout.
 * `now` (seconds) is passed in for deterministic, unit-testable behaviour. */
#define REPLAY_SLOTS 512

typedef struct {
    unsigned char nonce[16];
    long t;                 /* time recorded (0 = empty) */
} replay_slot_t;

typedef struct {
    replay_slot_t slots[REPLAY_SLOTS];
    int  next;              /* ring write position */
    long window;            /* seconds a nonce is remembered */
} replaycache_t;

void replaycache_init(replaycache_t *rc, long window_secs);

/* 1 = this nonce was already seen within the window (a replay — caller should drop);
 * 0 = fresh (it is recorded). */
int replaycache_seen(replaycache_t *rc, const unsigned char nonce[16], long now);

#endif
