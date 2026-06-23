---
id: FEAT-016
type: feature
priority: high
complexity: XL
estimate_tokens: 200k-400k
estimate_time: 240-480min
phase: planned
status: open
depends_on: [FEAT-013, FEAT-015]
milestone: MILESTONE-0.8.0
spawned_from: ~
---
# `norn_dial(pubkey) → session` connect orchestration

## Description

**As an** application
**I want** to open an encrypted channel to a peer **by its public key**
**So that** I never touch IP addresses, ports, NAT, or handshakes — the whole
point of norn.

Today the connect glue (resolve → punch → handshake → mux logical streams)
lives app-side in bifrost's `session.c` + `sio.c`. Lift a generic version into
norn so every consumer shares one connect path (Q1 decision).

## Implementation

- `norn_dial(client, pubkey) → norn_session_t*`: resolve endpoint via the DHT
  (signed record, FEAT-015), NAT-traverse (FEAT-017), run the channel
  handshake (FEAT-013), return an established encrypted session bound to the
  *verified* peer pubkey.
- `norn_listen()` / accept loop for inbound sessions.
- Over a session: `norn_stream_open()` (reliable, ordered == "TCP") and
  `norn_datagram_send()` (unreliable == "UDP"), multiplexing logical streams
  (generic `sio`/streammux lifted from bifrost).
- The only identity surfaced upward is the verified peer pubkey; authorization
  ("may this pubkey do X") stays in the application.

## Acceptance Criteria

1. Two norn clients dial each other by pubkey over loopback/UDP and exchange a
   reliable stream + a datagram, with no IP supplied by the caller beyond
   bootstrap.
2. The accept side learns the verified initiator pubkey.
3. Multiple logical streams mux over one session.
4. bifrost's `session`/`sio` can be retired in favour of the norn API
   (bifrost FEAT-080).

## Cross-repo

Consumed by bifrost FEAT-080 and wyrd FEAT-292.
