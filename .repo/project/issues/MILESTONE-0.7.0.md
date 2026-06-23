# v0.7.0 — Multi-Consumer Foundation

Enable norn to serve multiple projects (bifrost, wyrd) with different crypto/identity requirements.

## Status (2026-06-23)

**PLANNED** — Foundation for pluggable crypto and multi-identity support

## Tickets

| ID | Title | Priority | Depends On | Status |
|----|-------|----------|------------|--------|
| FEAT-013 | Pluggable crypto suite vtable | high | — | open |
| FEAT-014 | Parameterise Kademlia ID width | high | FEAT-013 | open |
| FEAT-015 | De-application-ise idexch | high | FEAT-013 | open |

## Overview

Currently norn hardcodes libsodium/Ed25519 throughout. This milestone introduces a
crypto suite vtable so that:

1. **bifrost** can continue using Ed25519/X25519/NaCl (no change)
2. **wyrd** can use secp256k1/ChaCha20-Poly1305 (Nostr-compatible)
3. Other consumers can bring their own primitives

## Architecture

### Crypto Suite Vtable

```c
typedef struct {
    size_t pubkey_len, sig_len, nodeid_len;
    
    /* Identity */
    int  (*sign)(unsigned char *sig, const unsigned char *msg, size_t len,
                 const unsigned char *sk);
    int  (*verify)(const unsigned char *sig, const unsigned char *msg, size_t len,
                   const unsigned char *pk);
    
    /* Ephemeral session crypto */
    int  (*eph_keygen)(unsigned char *eph_pk, unsigned char *eph_sk);
    int  (*ecdh)(unsigned char *shared, const unsigned char *my_eph_sk,
                 const unsigned char *peer_eph_pk);
    int  (*aead_seal)(unsigned char *out, const unsigned char *pt, size_t len,
                      const unsigned char *key, const unsigned char *nonce);
    int  (*aead_open)(unsigned char *out, const unsigned char *ct, size_t len,
                      const unsigned char *key, const unsigned char *nonce);
    
    /* DHT addressing */
    void (*nodeid_from_pubkey)(unsigned char *nodeid, const unsigned char *pubkey);
} norn_crypto_suite_t;
```

### Scope

- **FEAT-013**: Define vtable, thread through `channel.c`, native record signing, Kademlia node-id derivation
- **FEAT-014**: Generalise routing table for N-byte node IDs (wyrd: 264-bit pubkey-as-id)
- **FEAT-015**: Remove bifrost-specific concepts from `idexch.h` (account, ULA, CAP_VPN, BFID)

### Non-Goals

- The public mainline/BEP-44 client stays **fixed 20-byte Ed25519** (external spec)
- Only the **native overlay** gets parameterised crypto

## Acceptance Criteria

1. Suite installed via `norn_config_t` or `norn_new` param; NULL → default sodium suite
2. All crypto in `channel.c`, native record signing, and node-id derivation routes through suite
3. Existing bifrost/sodium tests pass unchanged with default suite
4. Mock suite test proves vtable is fully decoupled from libsodium

## Cross-Repo

- bifrost FEAT-079 (sodium suite) — consume norn with default suite
- wyrd FEAT-291 (secp256k1/ChaCha20 suite) — consume norn with custom suite

## Related Milestones

- **v0.8.0**: Dial & Session Orchestration (depends on FEAT-013, FEAT-015)
- **v0.9.0**: Tunnel & Bindings (depends on FEAT-016)
- **v0.10.0**: Private Overlay (depends on FEAT-014, FEAT-016)