# MILESTONE 0.3.0 — MULTI-CONSUMER BASE LAYER (DONE)

**Goal:** Turn norn from "bifrost's extracted P2P core" into a genuinely
application-agnostic base layer that multiple independent siblings
(bifrost, wyrd, and later bawee, thunder, mimir, dvalin, raven, regin,
njoerd) can build on.

**Mission (the north star):** *norn provides "TCP" and "UDP" addressed by
**public key** instead of by IP address.* You hand norn a peer's pubkey and
it gives you an encrypted reliable stream or unreliable datagram channel —
handling discovery (pubkey → endpoint), NAT traversal, and encryption
transparently. Everything that knows what a peer *is* or *may do*
(capabilities, accounts, trust, services, economics) is the **application's**
concern, not norn's.

## Background

norn was extracted from bifrost (a decentralised-tailscale VPN). As a result
its crypto is hardwired to libsodium (Ed25519 / X25519 / NaCl secretbox), its
DHT node-id is fixed at 20 bytes (SHA-1/256, mainline-shaped), and `idexch`
carries bifrost-specific concepts (`account`, VPN `ULA`, `CAP_VPN`, the `BFID`
wire magic). A second consumer — wyrd — needs secp256k1 identity +
ChaCha20-Poly1305 and a wider/own keyspace. The whole fleet around it is
secp256k1/Nostr-leaning, so bifrost's Ed25519 is the outlier, not wyrd.

## Settled design decisions

1. **Path X — crypto-agnostic.** norn gains a pluggable `norn_crypto_suite`
   vtable. bifrost installs the libsodium suite (unchanged default); wyrd
   installs a secp256k1 / ChaCha20-Poly1305 suite. The handshake *state
   machine* stays shared; only primitives swap.
2. **Decision 2a — apps keep their own keyspace.** Kademlia is parameterised
   on node-id width (from the suite). The **public mainline client stays
   fixed at Ed25519 / 20-byte** by BEP-44 spec and is used for bootstrap /
   interop only. bifrost happens to ride mainline *as* its overlay (20-byte
   ids); wyrd runs its own wider overlay and uses mainline for bootstrap only.
3. **Q1 — norn owns the full connect path.** `norn_dial(pubkey) → session`
   (resolve → NAT-punch → handshake → mux), not just the primitives. Generic
   `session`/`sio` lifted up from bifrost so apps don't each reinvent it.
4. **Q2 — relay + hole-punching move into norn, harmonised.** One
   crypto-agnostic implementation replacing bifrost's `circuit`/BEP-55 and
   wyrd's FEAT-114/FEAT-115 duplicates.
5. **DECISIONS #9 preserved.** bifrost and wyrd remain independent sibling
   *applications*; they now *share code* at the norn layer, but norn knows
   nothing about either. Adoption is opt-in per consumer.

## What stays OUT of norn (application concerns)

Capabilities, accounts, VPN ULAs, SSH `known_hosts`/`authorized_keys`,
reputation/trust scoring, capability tokens, service ACLs, pack/clan
membership, economic settlement. norn surfaces only the *verified peer
pubkey* + opaque app payloads.

## Features

| id | title | status |
|----|-------|--------|
| FEAT-013 | Pluggable `norn_crypto_suite` vtable (keystone) | [x] done |
| FEAT-014 | Parameterise Kademlia node-id width / keyspace | [x] done |
| FEAT-015 | De-application-ise `idexch` (opaque identity record + caps passthrough) | [x] done |
| FEAT-016 | `norn_dial(pubkey) → session` connect orchestration (generic session/sio) | [x] done |
| FEAT-017 | Harmonised NAT traversal — rendezvous hole-punch + onion relay | [x] done |
| FEAT-018 | Generic stream-tunnel utility (`norn-forward`) — service-over-pubkey | [x] done |
| FEAT-019 | Language binding — Rust crate (over the C SDK) | [x] done |
| FEAT-020 | Private-overlay bootstrap / fleet rendezvous | [x] done |

## Suggested order

FEAT-013 (crypto suite) is the keystone — nothing else lands cleanly without
it. Then FEAT-014 (keyspace) and FEAT-015 (idexch) unblock wyrd's identity;
FEAT-016 + FEAT-017 deliver the "dial(pubkey) through NAT" promise; FEAT-018
+ FEAT-019 make it consumable by the wider (non-C, HTTP-shaped) fleet;
FEAT-020 makes closed fleets practical.

## Consumer-side counterparts (tracked in the sibling repos)

- **bifrost:** `MILESTONE-NORN-BASE` (FEAT-079…082) — install libsodium
  suite, migrate onto shared dial/relay, move app-identity out of libnorn.
- **wyrd:** `MILESTONE-0.24-NORN` (FEAT-291…295) — secp256k1/ChaCha20 suite,
  `wyrd_datagram` → norn migration, overlay DHT on norn.
