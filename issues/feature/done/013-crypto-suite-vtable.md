---
id: FEAT-013
type: feature
priority: high
complexity: XL
estimate_tokens: 200k-400k
estimate_time: 240-480min
phase: done
status: done
depends_on: []
milestone: MILESTONE-0.7.0
spawned_from: ~
---
# Pluggable `norn_crypto_suite` vtable

## Description

**As a** norn integrator (bifrost, wyrd, …)
**I want** norn's identity, session-crypto and node-id derivation behind a
pluggable suite vtable
**So that** each application can use its own primitives (Ed25519/X25519/NaCl
for bifrost, secp256k1/ChaCha20-Poly1305 for wyrd and the Nostr-leaning fleet)
without forking norn.

Today crypto is hardwired to libsodium throughout `crypto.c`, `channel.c`,
`bep44.c`, `idexch.c`, and the Kademlia node-id derivation. This is the
keystone of MILESTONE-0.3.0 — nothing else lands cleanly until it exists.

## Implementation

Introduce a vtable installed at client creation:

```c
typedef struct {
    size_t pubkey_len, sig_len, nodeid_len;
    /* identity */
    int  (*sign)  (unsigned char *sig, const unsigned char *msg, size_t len,
                   const unsigned char *sk);
    int  (*verify)(const unsigned char *sig, const unsigned char *msg, size_t len,
                   const unsigned char *pk);
    /* ephemeral session crypto (suite knows how to ECDH from its identity) */
    int  (*eph_keygen)(unsigned char *eph_pk, unsigned char *eph_sk);
    int  (*ecdh)      (unsigned char *shared, const unsigned char *my_eph_sk,
                       const unsigned char *peer_eph_pk);
    int  (*aead_seal) (unsigned char *out, const unsigned char *pt, size_t len,
                       const unsigned char *key, const unsigned char *nonce);
    int  (*aead_open) (unsigned char *out, const unsigned char *ct, size_t len,
                       const unsigned char *key, const unsigned char *nonce);
    /* DHT addressing */
    void (*nodeid_from_pubkey)(unsigned char *nodeid, const unsigned char *pubkey);
} norn_crypto_suite_t;
```

- Thread the suite through `channel.c` (handshake + seal/open), the native
  BEP-44 record signing path, and Kademlia node-id derivation (see FEAT-014).
- Keep the **handshake state machine** (INIT/RESP/CONFIRM) shared; only the
  primitives swap.
- Ship the current libsodium implementation as the **default suite**
  (`norn_suite_sodium()`) so bifrost is a no-op migration.
- The public mainline/BEP-44 client is **excluded** — it stays Ed25519/SHA-1
  by external BEP-44 spec (see FEAT-014).

## Acceptance Criteria

1. A suite is installed via `norn_config_t` (or `norn_new` param); NULL → the
   default sodium suite.
2. `channel`, native record signing and node-id derivation all route through
   the suite — no direct libsodium calls remain on those paths.
3. Existing bifrost/sodium tests pass unchanged with the default suite.
4. A test suite stub (mock primitives) proves the vtable is fully decoupled.
5. Docs: `docs/CRYPTO-SUITE.md` defining the contract for suite authors.

## Cross-repo

Consumed by bifrost FEAT-079 (sodium suite) and wyrd FEAT-291
(secp256k1/ChaCha20 suite).
