# v0.8.0 — Dial & Session Orchestration

Connect by public key, not IP address.

## Status (2026-06-23)

**PLANNED** — Depends on v0.7.0 (FEAT-013, FEAT-015)

## Tickets

| ID | Title | Priority | Depends On | Status |
|----|-------|----------|------------|--------|
| FEAT-016 | norn_dial(pubkey) → session | high | FEAT-013, FEAT-015 | open |
| FEAT-017 | Harmonised NAT traversal | high | FEAT-013, FEAT-016 | open |

## Overview

Today the connect glue (resolve → punch → handshake → mux) lives app-side in
bifrost's `session.c` + `sio.c`. This milestone lifts a generic version into norn
so every consumer shares one connect path.

## Key Features

### FEAT-016: Dial by Pubkey

```c
norn_session_t *norn_dial(norn_client_t *client, const unsigned char *pubkey);
int norn_listen(norn_client_t *client);
norn_session_t *norn_accept(norn_client_t *client);

/* Over a session: */
norn_stream_t *norn_stream_open(norn_session_t *session);
int norn_datagram_send(norn_session_t *session, const void *data, size_t len);
```

The dial flow:
1. Resolve endpoint via DHT (signed record, FEAT-015)
2. NAT traverse (FEAT-017)
3. Run channel handshake (FEAT-013)
4. Return established encrypted session bound to verified peer pubkey

### FEAT-017: NAT Traversal

Two-layer approach:
1. **Rendezvous hole-punch** — Mutually-reachable peer signals both sides to fire probes (BEP-55-style)
2. **Onion relay fallback** — Fixed-size padded cells, layered AEAD per hop, all via crypto suite

Ladder: direct → hole-punch → relay

Relay/rendezvous capability is an opaque app cap (FEAT-015) — norn provides mechanism, app decides who relays.

## Architecture

```
norn_dial(pubkey)
    │
    ├─→ Resolve (DHT signed record)
    │
    ├─→ NAT Traversal
    │   ├─→ Direct (if public IP)
    │   ├─→ Hole-punch (rendezvous)
    │   └─→ Relay (onion circuit)
    │
    └─→ Channel Handshake (crypto suite)
        │
        └─→ norn_session_t (verified pubkey)
            │
            ├─→ Stream (TCP-like, reliable, ordered)
            └─→ Datagram (UDP-like, unreliable)
```

## Acceptance Criteria

1. Two norn clients dial each other by pubkey over loopback/UDP with no IP supplied beyond bootstrap
2. Accept side learns verified initiator pubkey
3. Multiple logical streams mux over one session
4. Symmetric NAT case establishes via rendezvous
5. Hole-punch failure falls back to relayed circuit; payload confidential to relay

## Cross-Repo

- bifrost FEAT-080 — retire `session.c`/`sio.c` in favor of norn API
- wyrd FEAT-292 — consume norn dial/listen

## Related Milestones

- **v0.7.0**: Crypto suite (FEAT-013, FEAT-015)
- **v0.9.0**: Tunnel & Bindings (depends on FEAT-016)
- **v0.10.0**: Private Overlay (depends on FEAT-016)