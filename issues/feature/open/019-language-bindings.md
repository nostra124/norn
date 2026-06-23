---
id: FEAT-019
type: feature
priority: medium
complexity: L
estimate_tokens: 120k-240k
estimate_time: 120-300min
phase: planned
status: open
depends_on: [FEAT-016]
milestone: MILESTONE-0.9.0
spawned_from: ~
---
# Language bindings — Rust crate + Python (over the C SDK)

## Description

**As a** non-C consumer (thunder, mimir, dvalin, regin are Rust; raven is
Python)
**I want** idiomatic bindings to norn
**So that** the fleet can adopt pubkey-addressed transport without writing C.

norn is C-only today; most of the intended fleet is Rust/Python. Follow the
established pattern in the ecosystem (`libmimir` C SDK + `mimir-lib` Rust FFI +
ergonomic `mimir` crate; `libbawee`).

## Implementation

- **Stable C SDK surface** for `norn_new`/`dial`/`listen`/stream/datagram/
  publish/resolve + suite installation. Version the ABI.
- **Rust crate**: safe wrapper; expose a norn stream as tokio
  `AsyncRead + AsyncWrite` and a listener as an accept loop, so
  `axum::serve(norn_listener, app)` and hyper-over-norn "just work"
  (the clean in-process counterpart to FEAT-018). Bridge norn's event loop to
  the host runtime.
- **Python binding**: thin cffi/ctypes wrapper over the C SDK for raven.

## Acceptance Criteria

1. A Rust example dials a peer by pubkey and serves an axum app over a norn
   stream (no TCP listener, no TLS).
2. A Python example publishes/resolves and exchanges a stream.
3. Bindings build in CI on Linux + macOS.

## Cross-repo

Enables thunder/mimir/regin/dvalin (Rust) and raven (Python) to consume norn.
