#ifndef CHANNEL_H
#define CHANNEL_H

#include <stddef.h>
#include "replaycache.h"   /* replaycache_t — gates replays of the 0-RTT first flight */

/* Secure peer channel (FEAT-002, DECISIONS #1): an ephemeral X25519 ECDH
 * (forward secrecy) yields per-direction session keys; data is sealed with
 * NaCl secretbox. The ephemeral pubkeys are exchanged and authenticated with
 * the ed25519 identity in the handshake (next slice). This file is just the
 * session-crypto core. */

#define CHANNEL_PUBKEYBYTES 32      /* X25519 ephemeral public key */
#define CHANNEL_OVERHEAD 40         /* secretbox nonce (24) + MAC (16) per message */

typedef struct {
    unsigned char eph_pub[32];      /* our ephemeral X25519 public key */
    unsigned char eph_sec[32];      /* our ephemeral X25519 secret key */
    unsigned char peer_eph[32];     /* peer's ephemeral X25519 public key */
    unsigned char rx_key[32];       /* session key for opening peer messages */
    unsigned char tx_key[32];       /* session key for sealing our messages */
    int is_initiator;               /* 1 = connect side, 0 = accept side */
    int established;
} channel_t;

/* Generate a fresh ephemeral keypair (eph_pub is shared in the handshake). */
int channel_gen_ephemeral(channel_t *c);

/* Derive the per-direction session keys from our ephemeral secret and the
 * peer's ephemeral public key. The two ends must pass opposite is_initiator
 * values (the connect side = 1, the accept side = 0). Returns 0 on success. */
int channel_derive(channel_t *c, const unsigned char *peer_eph_pub, int is_initiator);

#define CHANNEL_RESUMEBYTES 32

/* FEAT-075: derive a per-session resumption secret from an established channel.
 * Order-independent in the two directional keys, so both peers compute the SAME
 * value (the connect and accept sides have rx/tx swapped). Cache it per peer to
 * seed a future 0-RTT reconnect (PSK + fresh ephemeral). Requires c->established;
 * returns 0 on success, -1 otherwise. */
int channel_resumption_secret(const channel_t *c, unsigned char out[CHANNEL_RESUMEBYTES]);

/* FEAT-075: derive session keys for a 0-RTT reconnect — a normal ephemeral key
 * exchange (channel_derive) with the cached per-peer resumption secret (PSK)
 * mixed into each directional key. Both ends pass the same PSK and opposite
 * is_initiator, and (as with channel_derive) compute paired keys. A wrong/stale
 * PSK yields non-matching keys, so the peer simply can't open the first flight
 * and the caller falls back to a full handshake. Returns 0 on success. */
int channel_derive_resumption(channel_t *c, const unsigned char *peer_eph_pub,
                              int is_initiator, const unsigned char psk[CHANNEL_RESUMEBYTES]);

/* FEAT-075 (3/n): derive the 0-RTT *early-data* directional keys from the PSK and the
 * initiator's ephemeral public key ALONE — no responder ephemeral exists yet when the
 * first flight is sealed, so this is NOT forward-secret (the TLS-1.3 0-RTT caveat): it
 * is permitted only for an idempotent opener and gated by replay protection. Both ends
 * compute the same keys (the initiator seals with tx_key, the responder opens with
 * rx_key). Leaves the ephemeral/peer-ephemeral fields untouched so the SAME channel completes the full
 * ephemeral handshake afterwards (which re-keys the session for forward secrecy). */
void channel_derive_0rtt(channel_t *c, const unsigned char *init_eph,
                         int is_initiator, const unsigned char psk[CHANNEL_RESUMEBYTES]);

#define CHANNEL_SIGBYTES 64

/* Handshake auth: sign / verify a proof that binds the two ephemeral pubkeys to
 * an ed25519 identity. The transcript is init_eph(32) || resp_eph(32) (initiator's
 * ephemeral first), so both ends compute the same bytes after the exchange.
 * self_sk is the ed25519 secret (CRYPTO_SECRETKEYBYTES); peer_pk the ed25519
 * public (32). Both return 0 on success/valid, non-zero otherwise. */
int channel_auth_sign(unsigned char *sig, const unsigned char *self_sk,
                      const unsigned char *init_eph, const unsigned char *resp_eph);
int channel_auth_verify(const unsigned char *sig, const unsigned char *peer_pk,
                        const unsigned char *init_eph, const unsigned char *resp_eph);

/* ---- Handshake wire protocol (FEAT-002 slice 2b) ----------------------------
 *
 * A 3-message mutual authentication over the (UDP) endpoint. Both ends carry an
 * ed25519 identity; ephemeral X25519 keys give forward secrecy and are bound to
 * the identity by the signed transcript (init_eph||resp_eph).
 *
 *   initiator                                responder
 *     --- INIT: init_pub, init_eph ------------>          accept: gen eph, derive,
 *     <-- RESP: resp_pub, resp_eph, resp_sig ---          sign transcript
 *   verify resp_sig (then TOFU resp_pub),
 *   derive, sign transcript
 *     --- CONFIRM: seal(init_sig) ------------->          open + verify init_sig
 *                                                         (then check authorized_keys)
 *
 * After CONFIRM both channels are established and seal/open works. The caller is
 * responsible for the trust checks: the initiator verifies resp_pub against
 * known_hosts (TOFU) once channel_hs_confirm returns it; the responder verifies
 * the initiator's pub against authorized_keys once channel_hs_finish succeeds.
 */
#define CHANNEL_MAGIC      0x44484348u  /* "DHCH" */
#define CHANNEL_MSG_INIT     1
#define CHANNEL_MSG_RESP     2
#define CHANNEL_MSG_CONFIRM  3
#define CHANNEL_MSG_INIT_VPN 5  /* INIT for a retained VPN session (no fork) */
#define CHANNEL_MSG_INIT_0RTT 6 /* INIT carrying a PSK-sealed 0-RTT first flight (FEAT-075) */
#define CHANNEL_INIT_LEN     (4 + 1 + 32 + 32)              /* 69  */
#define CHANNEL_RESP_LEN     (4 + 1 + 32 + 32 + 64)         /* 133 */
#define CHANNEL_CONFIRM_LEN  (4 + 1 + 24 + 64 + 16)         /* 109 (sealed 64-byte sig) */

/* Initiator: build INIT. Generates our ephemeral into c. self_pub = our ed25519
 * public (32). Returns CHANNEL_INIT_LEN or -1. */
int channel_hs_build_init(channel_t *c, const unsigned char *self_pub,
                          unsigned char *out, size_t outcap);

/* As channel_hs_build_init but with an explicit INIT message type (e.g.
 * CHANNEL_MSG_INIT_VPN so the responder routes it to the retained VPN path). */
int channel_hs_build_init_ex(channel_t *c, const unsigned char *self_pub,
                             int msg_type, unsigned char *out, size_t outcap);

/* Responder: handle INIT and build RESP. Generates our ephemeral, derives the
 * session keys, signs the transcript with self_sk. Writes the initiator's
 * claimed ed25519 pub to peer_pub_out (32). Returns CHANNEL_RESP_LEN or -1. */
int channel_hs_accept(channel_t *c, const unsigned char *self_pub,
                      const unsigned char *self_sk,
                      const unsigned char *init_msg, size_t init_len,
                      unsigned char *peer_pub_out,
                      unsigned char *out, size_t outcap);

/* ---- 0-RTT first flight (FEAT-075 3/n) -------------------------------------
 * On a reconnect to a peer for which a resumption PSK is cached, the initiator
 * seals idempotent early application data into the INIT itself (one message, zero
 * round-trips to first byte). The clear prefix is byte-identical to a plain INIT
 * (only the type byte differs), so a responder that cannot resume still runs a full
 * handshake from init_pub+init_eph and simply drops the early data (0-RTT reject). */
#define CHANNEL_0RTT_OK     0   /* early data opened and delivered */
#define CHANNEL_0RTT_NOPSK  1   /* no PSK supplied — full handshake, no early data */
#define CHANNEL_0RTT_BADPSK 2   /* wrong/stale PSK — could not open — fall back cleanly */
#define CHANNEL_0RTT_REPLAY 3   /* a replayed first flight — early data rejected */

/* Initiator: build a 0-RTT INIT carrying `early`/`early_len` sealed under a PSK-only
 * early-data key. Generates our ephemeral into c (the same c continues into
 * channel_hs_confirm, whose real ECDH overwrites the transient early keys). Returns
 * the total length (CHANNEL_INIT_LEN + CHANNEL_OVERHEAD + early_len) or -1. */
int channel_hs_build_init_0rtt(channel_t *c, const unsigned char *self_pub,
                               const unsigned char *psk,
                               const unsigned char *early, size_t early_len,
                               unsigned char *out, size_t outcap);

/* Responder: handle a 0-RTT INIT. ALWAYS builds RESP into out (CHANNEL_RESP_LEN) so
 * the full ephemeral handshake proceeds for forward secrecy of the rest of the
 * session, exactly like channel_hs_accept; peer_pub_out gets the initiator's claimed
 * ed25519 pub. If `psk` is non-NULL and the sealed first flight opens and is not a
 * replay, the early data is written to early_out and *early_len set; otherwise
 * *early_len = 0. `rpc` (may be NULL) records/rejects the first-flight nonce. The
 * optional *status reports CHANNEL_0RTT_{OK,NOPSK,BADPSK,REPLAY}. Returns
 * CHANNEL_RESP_LEN or -1 on bad input. */
int channel_hs_accept_0rtt(channel_t *c, const unsigned char *self_pub,
                           const unsigned char *self_sk,
                           const unsigned char *init_msg, size_t init_len,
                           const unsigned char *psk,
                           replaycache_t *rpc, long now,
                           unsigned char *peer_pub_out,
                           unsigned char *early_out, size_t early_cap, size_t *early_len,
                           int *status,
                           unsigned char *out, size_t outcap);

/* Initiator: handle RESP and build CONFIRM. Verifies resp_sig against the
 * responder's claimed pub, derives session keys, seals our own transcript
 * signature. Writes resp_pub to peer_pub_out (32); the caller must then check it
 * against known_hosts. Returns CHANNEL_CONFIRM_LEN or -1 (incl. bad sig). */
int channel_hs_confirm(channel_t *c, const unsigned char *self_pub,
                       const unsigned char *self_sk,
                       const unsigned char *resp_msg, size_t resp_len,
                       unsigned char *peer_pub_out,
                       unsigned char *out, size_t outcap);

/* Responder: handle CONFIRM. Opens the sealed signature and verifies it against
 * the initiator's pub (from channel_hs_accept). Returns 0 if the initiator is
 * authenticated — the caller must then check the pub against authorized_keys. */
int channel_hs_finish(channel_t *c, const unsigned char *init_pub,
                      const unsigned char *confirm_msg, size_t confirm_len);

/* Seal plaintext into out (needs ptlen + CHANNEL_OVERHEAD bytes). Returns the
 * sealed length, or -1. */
int channel_seal(channel_t *c, const unsigned char *pt, size_t ptlen,
                 unsigned char *out, size_t outcap);

/* Open a sealed message into out. Returns the plaintext length, or -1 on
 * authentication failure / bad input. */
int channel_open(channel_t *c, const unsigned char *ct, size_t ctlen,
                 unsigned char *out, size_t outcap);

/* Try to open `ct` with each of the `n` channels; on the first that authenticates,
 * write the plaintext to `out` and return that channel's index. Returns -1 if none
 * open it. Used for VPN endpoint roaming (BUG-142): identify which established peer
 * a sealed packet from an unknown source address belongs to — the secretbox auth is
 * the identity proof, so a wrong channel can never falsely match. */
int channel_open_any(channel_t *chs[], int n, const unsigned char *ct, size_t ctlen,
                     unsigned char *out, size_t outcap);

#endif
