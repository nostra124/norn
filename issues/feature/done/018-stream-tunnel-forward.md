---
id: FEAT-018
type: feature
priority: medium
complexity: M
estimate_tokens: 60k-120k
estimate_time: 90-180min
phase: done
status: done
depends_on: [FEAT-016]
milestone: MILESTONE-0.9.0
spawned_from: ~
---
# Generic stream-tunnel utility (`norn-forward`) — service-over-pubkey

## Description

**As a** fleet app exposing an existing TCP service (e.g. thunder's HTTP API)
**I want** to reach it by pubkey with zero code changes
**So that** any HTTP/JSON/line protocol rides norn like it rides TCP.

HTTP is just an app protocol over a reliable stream, and a norn stream *is*
TCP-equivalent. Lift bifrost's `forward`/`SVC_TUNNEL` (ssh -L/-R) into norn as
a reusable utility.

## Implementation

- Server side: `listen()` on norn; per inbound session open a local
  TCP/Unix connection to the wrapped service and pump bytes both ways.
- Client side: listen on a local socket; forward each connection to
  `norn_dial(pubkey)`.
- Ship as a small `norn-forward` binary **and** a library entry so apps can
  embed it. TLS is dropped (norn already encrypts E2E).
- Expose the verified peer pubkey to the wrapped service (env/header hook) so
  apps can use it as transport-level identity.

## Acceptance Criteria

1. `curl http://localhost:PORT` tunnels over norn to an unmodified HTTP server
   on a remote peer addressed only by pubkey.
2. Bidirectional, length-correct byte transfer under load.
3. Peer pubkey is available to the wrapped service.

## Implementation Status

- **Phase 1 — stream multiplexing** (`streammux`): ✅ done.
- **Phase 2 — splice engine** (`norn_forward.{c,h}`): ✅ done. A pure,
  transport-agnostic bidirectional byte pump (half-close/EOF, bounded
  backpressured buffers), unit-tested to 100% line+branch coverage with
  in-memory fakes (`tests/test_forward.c`). Shipped as a library entry so apps
  can embed it.
- **Phase 3 — `norn-forward` CLI**: ✅ client side (`-L`: local TCP listen →
  dial peer by pubkey → splice) **and** server side (`-R`: accept inbound peer
  streams → connect the wrapped local service → splice). The server side rides
  the new `norn_session_set_accept_stream()` inbound-stream API; the underlying
  session stream data plane is verified end-to-end by
  `tests/test_session_loopback.c`.
- Acceptance criterion 3 (expose verified peer pubkey to the wrapped service)
  is available via `norn_session_get_peer()` on the accepted session.

## Cross-repo

Day-1 "HTTP over libnorn" answer for thunder, mimir, regin, raven. The clean
in-process alternative (axum/hyper over a norn stream) is delivered by the
Rust binding, FEAT-019.
