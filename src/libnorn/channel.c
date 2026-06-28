#include "channel.h"
#include "crypto.h"
#include <sodium.h>
#include <string.h>
#include <stdint.h>

int channel_resumption_secret(const channel_t *c, unsigned char out[CHANNEL_RESUMEBYTES]) {
    if (!c || !c->established || !out) return -1;
    /* The two ends have rx_key/tx_key swapped, so combine them order-independently
     * (lexicographically smaller key first) to get a value both peers agree on. */
    const unsigned char *lo = c->rx_key, *hi = c->tx_key;
    if (memcmp(lo, hi, 32) > 0) { const unsigned char *t = lo; lo = hi; hi = t; }
    static const unsigned char label[] = "bifrost-resume-v1";
    crypto_generichash_state st;
    crypto_generichash_init(&st, NULL, 0, CHANNEL_RESUMEBYTES);
    crypto_generichash_update(&st, label, sizeof(label) - 1);
    crypto_generichash_update(&st, lo, 32);
    crypto_generichash_update(&st, hi, 32);
    crypto_generichash_final(&st, out, CHANNEL_RESUMEBYTES);
    return 0;
}

/* mix the resumption PSK into one directional key: key <- H(label || psk || key).
 * The same transform on both ends preserves the kx pairing (initiator.tx ==
 * responder.rx), while a different PSK diverges the result. */
static void resume_mix(unsigned char key[32], const unsigned char psk[CHANNEL_RESUMEBYTES]) {
    static const unsigned char label[] = "bifrost-0rtt-v1";
    crypto_generichash_state st;
    crypto_generichash_init(&st, NULL, 0, 32);
    crypto_generichash_update(&st, label, sizeof(label) - 1);
    crypto_generichash_update(&st, psk, CHANNEL_RESUMEBYTES);
    crypto_generichash_update(&st, key, 32);
    crypto_generichash_final(&st, key, 32);
}

int channel_derive_resumption(channel_t *c, const unsigned char *peer_eph_pub,
                              int is_initiator, const unsigned char psk[CHANNEL_RESUMEBYTES]) {
    if (!c || !peer_eph_pub || !psk) return -1;
    if (channel_derive(c, peer_eph_pub, is_initiator) != 0) return -1;  /* base ephemeral kx */
    resume_mix(c->rx_key, psk);
    resume_mix(c->tx_key, psk);
    return 0;
}

/* one 0-RTT early-data directional key: H(dir-label || psk || init_eph). No ECDH —
 * the responder's ephemeral does not exist when the first flight is sealed. */
static void early_key(unsigned char out[32], const char *dir,
                      const unsigned char psk[CHANNEL_RESUMEBYTES],
                      const unsigned char init_eph[32]) {
    crypto_generichash_state st;
    crypto_generichash_init(&st, NULL, 0, 32);
    crypto_generichash_update(&st, (const unsigned char *)dir, strlen(dir));
    crypto_generichash_update(&st, psk, CHANNEL_RESUMEBYTES);
    crypto_generichash_update(&st, init_eph, 32);
    crypto_generichash_final(&st, out, 32);
}

void channel_derive_0rtt(channel_t *c, const unsigned char *init_eph,
                         int is_initiator, const unsigned char psk[CHANNEL_RESUMEBYTES]) {
    if (!c || !init_eph || !psk) return;
    unsigned char k_i2r[32], k_r2i[32];
    early_key(k_i2r, "bifrost-0rtt-early-i2r-v1", psk, init_eph);
    early_key(k_r2i, "bifrost-0rtt-early-r2i-v1", psk, init_eph);
    if (is_initiator) { memcpy(c->tx_key, k_i2r, 32); memcpy(c->rx_key, k_r2i, 32); }
    else              { memcpy(c->rx_key, k_i2r, 32); memcpy(c->tx_key, k_r2i, 32); }
    c->established = 1;   /* enough for seal/open; ephemeral fields left for the full HS */
    sodium_memzero(k_i2r, sizeof k_i2r);
    sodium_memzero(k_r2i, sizeof k_r2i);
}

int channel_hs_build_init_0rtt(channel_t *c, const unsigned char *self_pub,
                               const unsigned char *psk,
                               const unsigned char *early, size_t early_len,
                               unsigned char *out, size_t outcap) {
    if (!c || !self_pub || !psk || (!early && early_len) || !out) return -1;
    if (outcap < CHANNEL_INIT_LEN + CHANNEL_OVERHEAD + early_len) return -1;
    if (channel_hs_build_init_ex(c, self_pub, CHANNEL_MSG_INIT_0RTT, out, outcap) != CHANNEL_INIT_LEN) return -1;   /* LCOV_EXCL_BR_LINE: args+outcap pre-validated */
    /* seal the first flight with the PSK-only early-data key over our fresh ephemeral
     * (now in c->eph_pub). A scratch channel keeps c's keys clean for the full HS. */
    channel_t tmp;
    memset(&tmp, 0, sizeof tmp);
    channel_derive_0rtt(&tmp, c->eph_pub, 1, psk);
    int sl = channel_seal(&tmp, early, early_len, out + CHANNEL_INIT_LEN, outcap - CHANNEL_INIT_LEN);
    sodium_memzero(&tmp, sizeof tmp);
    if (sl < 0) return -1;   /* LCOV_EXCL_BR_LINE: outcap guarantees room for the sealed flight */
    return CHANNEL_INIT_LEN + sl;
}

int channel_hs_accept_0rtt(channel_t *c, const unsigned char *self_pub,
                           const unsigned char *self_sk,
                           const unsigned char *init_msg, size_t init_len,
                           const unsigned char *psk,
                           replaycache_t *rpc, long now,
                           unsigned char *peer_pub_out,
                           unsigned char *early_out, size_t early_cap, size_t *early_len,
                           int *status,
                           unsigned char *out, size_t outcap) {
    if (early_len) *early_len = 0;
    int st = CHANNEL_0RTT_NOPSK;
    if (!c || !self_pub || !self_sk || !init_msg || !peer_pub_out || !out) return -1;
    if (init_len < CHANNEL_INIT_LEN) return -1;
    if (init_msg[4] != CHANNEL_MSG_INIT_0RTT) return -1;

    const unsigned char *init_eph = init_msg + 5 + 32;

    /* Build RESP via the normal accept path. It re-validates the magic and routes on
     * the type byte (INIT / INIT_VPN); present a plain-INIT view of the clear prefix so
     * the full ephemeral handshake proceeds (the type byte is not in the signed
     * transcript, so the initiator's CONFIRM still verifies). */
    unsigned char hdr[CHANNEL_INIT_LEN];
    memcpy(hdr, init_msg, CHANNEL_INIT_LEN);
    hdr[4] = CHANNEL_MSG_INIT;
    if (channel_hs_accept(c, self_pub, self_sk, hdr, CHANNEL_INIT_LEN,
                          peer_pub_out, out, outcap) != CHANNEL_RESP_LEN) return -1;

    /* Try to open the sealed first flight in a scratch channel (independent of c's
     * real handshake keys). */
    if (psk) {
        const unsigned char *sealed = init_msg + CHANNEL_INIT_LEN;
        size_t sealed_len = init_len - CHANNEL_INIT_LEN;
        channel_t tmp;
        memset(&tmp, 0, sizeof tmp);
        channel_derive_0rtt(&tmp, init_eph, 0, psk);
        int ol = (early_out && early_cap)
                   ? channel_open(&tmp, sealed, sealed_len, early_out, early_cap) : -1;
        sodium_memzero(&tmp, sizeof tmp);
        if (ol < 0) {
            st = CHANNEL_0RTT_BADPSK;        /* wrong/stale PSK (or no room) — fall back */
        } else if (rpc && replaycache_seen(rpc, sealed, now)) {
            /* a successfully-opened flight is ≥ CHANNEL_OVERHEAD bytes, so the 16-byte
             * nonce the replay cache reads is always present. */
            st = CHANNEL_0RTT_REPLAY;        /* the secretbox nonce was seen before */
        } else {
            st = CHANNEL_0RTT_OK;
            if (early_len) *early_len = (size_t)ol;
        }
    }
    if (status) *status = st;
    return CHANNEL_RESP_LEN;
}

/* Build the signed transcript (init_eph || resp_eph) and hand it to `sign`. */
static int auth_sign_with(unsigned char *sig, channel_signer_fn sign, void *ud,
                          const unsigned char *init_eph,
                          const unsigned char *resp_eph) {
    unsigned char tr[64];
    memcpy(tr, init_eph, 32);
    memcpy(tr + 32, resp_eph, 32);
    return sign(ud, sig, tr, sizeof(tr));
}

/* Built-in signer: ed25519-sign with a raw secret key passed as `ud`. */
static int secret_signer(void *ud, unsigned char sig[CHANNEL_SIGBYTES],
                         const unsigned char *msg, size_t msglen) {
    return bf_sign(sig, msg, msglen, (const unsigned char *)ud);
}

int channel_auth_sign(unsigned char *sig, const unsigned char *self_sk,
                      const unsigned char *init_eph, const unsigned char *resp_eph) {
    if (!sig || !self_sk || !init_eph || !resp_eph) return -1;
    return auth_sign_with(sig, secret_signer, (void *)self_sk, init_eph, resp_eph);
}

int channel_auth_verify(const unsigned char *sig, const unsigned char *peer_pk,
                        const unsigned char *init_eph, const unsigned char *resp_eph) {
    if (!sig || !peer_pk || !init_eph || !resp_eph) return -1;
    unsigned char tr[64];
    memcpy(tr, init_eph, 32);
    memcpy(tr + 32, resp_eph, 32);
    return bf_verify(sig, tr, sizeof(tr), peer_pk);
}

int channel_gen_ephemeral(channel_t *c) {
    if (!c) return -1;
    memset(c, 0, sizeof(*c));
    if (crypto_kx_keypair(c->eph_pub, c->eph_sec) != 0) return -1;   /* LCOV_EXCL_BR_LINE: keypair never fails */
    return 0;
}

int channel_derive(channel_t *c, const unsigned char *peer_eph_pub, int is_initiator) {
    if (!c || !peer_eph_pub) return -1;
    int rc = is_initiator
        ? crypto_kx_client_session_keys(c->rx_key, c->tx_key, c->eph_pub, c->eph_sec, peer_eph_pub)
        : crypto_kx_server_session_keys(c->rx_key, c->tx_key, c->eph_pub, c->eph_sec, peer_eph_pub);
    if (rc != 0) return -1;
    memcpy(c->peer_eph, peer_eph_pub, 32);
    c->is_initiator = is_initiator;
    c->established = 1;
    return 0;
}

/* Reconstruct the signed transcript (init_eph || resp_eph) from c's role. */
static void channel_transcript(const channel_t *c, unsigned char *init_eph,
                               unsigned char *resp_eph) {
    if (c->is_initiator) {
        memcpy(init_eph, c->eph_pub, 32);
        memcpy(resp_eph, c->peer_eph, 32);
    } else {
        memcpy(init_eph, c->peer_eph, 32);
        memcpy(resp_eph, c->eph_pub, 32);
    }
}

static void put_u32(unsigned char *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff; p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;  p[3] = v & 0xff;
}
static uint32_t get_u32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int channel_hs_build_init_ex(channel_t *c, const unsigned char *self_pub,
                             int msg_type, unsigned char *out, size_t outcap) {
    if (!c || !self_pub || !out || outcap < CHANNEL_INIT_LEN) return -1;
    if (channel_gen_ephemeral(c) != 0) return -1;   /* LCOV_EXCL_BR_LINE: gen never fails here (c checked) */
    unsigned char *p = out;
    put_u32(p, CHANNEL_MAGIC); p += 4;
    *p++ = (unsigned char)msg_type;
    memcpy(p, self_pub, 32); p += 32;
    memcpy(p, c->eph_pub, 32);
    return CHANNEL_INIT_LEN;
}

int channel_hs_build_init(channel_t *c, const unsigned char *self_pub,
                          unsigned char *out, size_t outcap) {
    return channel_hs_build_init_ex(c, self_pub, CHANNEL_MSG_INIT, out, outcap);
}

int channel_hs_accept_signed(channel_t *c, const unsigned char *self_pub,
                             channel_signer_fn sign, void *sign_ud,
                             const unsigned char *init_msg, size_t init_len,
                             unsigned char *peer_pub_out,
                             unsigned char *out, size_t outcap) {
    if (!c || !self_pub || !sign || !init_msg || !peer_pub_out || !out) return -1;
    if (init_len != CHANNEL_INIT_LEN || outcap < CHANNEL_RESP_LEN) return -1;
    const unsigned char *p = init_msg;
    if (get_u32(p) != CHANNEL_MAGIC) return -1;
    p += 4;
    if (*p != CHANNEL_MSG_INIT && *p != CHANNEL_MSG_INIT_VPN) return -1;
    p++;
    memcpy(peer_pub_out, p, 32); p += 32;        /* initiator's ed25519 pub */
    const unsigned char *init_eph = p;           /* initiator's ephemeral */

    if (channel_gen_ephemeral(c) != 0) return -1;   /* LCOV_EXCL_BR_LINE: gen never fails here */
    if (channel_derive(c, init_eph, 0) != 0) return -1;  /* responder side */

    unsigned char sig[CHANNEL_SIGBYTES];
    /* The signer can fail (e.g. ssh-agent unreachable); the built-in raw-secret
     * signer never does. */
    if (auth_sign_with(sig, sign, sign_ud, init_eph, c->eph_pub) != 0) return -1;

    unsigned char *o = out;
    put_u32(o, CHANNEL_MAGIC); o += 4;
    *o++ = CHANNEL_MSG_RESP;
    memcpy(o, self_pub, 32); o += 32;
    memcpy(o, c->eph_pub, 32); o += 32;
    memcpy(o, sig, CHANNEL_SIGBYTES);
    return CHANNEL_RESP_LEN;
}

int channel_hs_accept(channel_t *c, const unsigned char *self_pub,
                      const unsigned char *self_sk,
                      const unsigned char *init_msg, size_t init_len,
                      unsigned char *peer_pub_out,
                      unsigned char *out, size_t outcap) {
    if (!self_sk) return -1;
    return channel_hs_accept_signed(c, self_pub, secret_signer, (void *)self_sk,
                                    init_msg, init_len, peer_pub_out, out, outcap);
}

int channel_hs_confirm_signed(channel_t *c, const unsigned char *self_pub,
                              channel_signer_fn sign, void *sign_ud,
                              const unsigned char *resp_msg, size_t resp_len,
                              unsigned char *peer_pub_out,
                              unsigned char *out, size_t outcap) {
    if (!c || !self_pub || !sign || !resp_msg || !peer_pub_out || !out) return -1;
    if (resp_len != CHANNEL_RESP_LEN || outcap < CHANNEL_CONFIRM_LEN) return -1;
    const unsigned char *p = resp_msg;
    if (get_u32(p) != CHANNEL_MAGIC) return -1;
    p += 4;
    if (*p++ != CHANNEL_MSG_RESP) return -1;
    memcpy(peer_pub_out, p, 32); p += 32;        /* responder's ed25519 pub */
    const unsigned char *resp_eph = p; p += 32;
    const unsigned char *resp_sig = p;

    /* verify the responder bound resp_eph to its identity over our init_eph */
    if (channel_auth_verify(resp_sig, peer_pub_out, c->eph_pub, resp_eph) != 0) return -1;
    if (channel_derive(c, resp_eph, 1) != 0) return -1;  /* LCOV_EXCL_BR_LINE: resp_eph already auth'd, derive ok */

    unsigned char sig[CHANNEL_SIGBYTES];
    /* The signer can fail (e.g. ssh-agent unreachable); the built-in raw-secret
     * signer never does. */
    if (auth_sign_with(sig, sign, sign_ud, c->eph_pub, resp_eph) != 0) return -1;

    unsigned char *o = out;
    put_u32(o, CHANNEL_MAGIC); o += 4;
    *o++ = CHANNEL_MSG_CONFIRM;
    int sl = channel_seal(c, sig, CHANNEL_SIGBYTES, o, outcap - 5);
    if (sl != CHANNEL_SIGBYTES + CHANNEL_OVERHEAD) return -1;   /* LCOV_EXCL_BR_LINE: outcap≥CONFIRM_LEN ⇒ seal fits */
    return CHANNEL_CONFIRM_LEN;
}

int channel_hs_confirm(channel_t *c, const unsigned char *self_pub,
                       const unsigned char *self_sk,
                       const unsigned char *resp_msg, size_t resp_len,
                       unsigned char *peer_pub_out,
                       unsigned char *out, size_t outcap) {
    if (!self_sk) return -1;
    return channel_hs_confirm_signed(c, self_pub, secret_signer, (void *)self_sk,
                                     resp_msg, resp_len, peer_pub_out, out, outcap);
}

int channel_hs_finish(channel_t *c, const unsigned char *init_pub,
                      const unsigned char *confirm_msg, size_t confirm_len) {
    if (!c || !c->established || !init_pub || !confirm_msg) return -1;
    if (confirm_len != CHANNEL_CONFIRM_LEN) return -1;
    const unsigned char *p = confirm_msg;
    if (get_u32(p) != CHANNEL_MAGIC) return -1;
    p += 4;
    if (*p++ != CHANNEL_MSG_CONFIRM) return -1;

    unsigned char sig[CHANNEL_SIGBYTES];
    int ol = channel_open(c, p, CHANNEL_SIGBYTES + CHANNEL_OVERHEAD, sig, sizeof(sig));
    if (ol != CHANNEL_SIGBYTES) return -1;

    unsigned char init_eph[32], resp_eph[32];
    channel_transcript(c, init_eph, resp_eph);
    return channel_auth_verify(sig, init_pub, init_eph, resp_eph);
}

int channel_seal(channel_t *c, const unsigned char *pt, size_t ptlen,
                 unsigned char *out, size_t outcap) {
    if (!c || !c->established || !out) return -1;
    size_t need = crypto_secretbox_NONCEBYTES + ptlen + crypto_secretbox_MACBYTES;
    if (outcap < need) return -1;
    randombytes_buf(out, crypto_secretbox_NONCEBYTES);  /* fresh nonce, prepended */
    if (crypto_secretbox_easy(out + crypto_secretbox_NONCEBYTES, pt, ptlen, out, c->tx_key) != 0) return -1;   /* LCOV_EXCL_BR_LINE: never fails */
    return (int)need;
}

int channel_open(channel_t *c, const unsigned char *ct, size_t ctlen,
                 unsigned char *out, size_t outcap) {
    if (!c || !c->established || !ct) return -1;
    if (ctlen < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) return -1;
    size_t clen = ctlen - crypto_secretbox_NONCEBYTES;
    size_t mlen = clen - crypto_secretbox_MACBYTES;
    if (outcap < mlen) return -1;
    if (crypto_secretbox_open_easy(out, ct + crypto_secretbox_NONCEBYTES, clen, ct, c->rx_key) != 0)
        return -1;
    return (int)mlen;
}

int channel_open_any(channel_t *chs[], int n, const unsigned char *ct, size_t ctlen,
                     unsigned char *out, size_t outcap) {
    if (!chs) return -1;
    for (int i = 0; i < n; i++) {
        if (chs[i] && channel_open(chs[i], ct, ctlen, out, outcap) >= 0) return i;
    }
    return -1;
}
