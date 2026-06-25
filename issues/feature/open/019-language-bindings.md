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
# Language bindings — Rust crate (over the C SDK)

## Description

**As a** non-C consumer (thunder, mimir, dvalin, regin are Rust)
**I want** an idiomatic Rust binding to norn
**So that** the fleet can adopt pubkey-addressed transport without writing C.

norn is C-only today; the intended fleet is Rust-leaning. Follow the
established pattern in the ecosystem (`libmimir` C SDK + `mimir-lib` Rust FFI +
ergonomic `mimir` crate; `libbawee`).

> **Scope note:** a Python binding was previously planned here but has been
> dropped — not needed for the foreseeable future. This feature is Rust-only.

## Implementation

- **Stable C SDK surface** for `norn_new`/`dial`/`listen`/stream/datagram/
  publish/resolve + suite installation. Version the ABI.
- **`-sys` crate**: raw FFI declarations + `build.rs` linking `libnorn`
  (pkg-config / `NORN_LIB_DIR`).
- **Rust crate**: safe wrapper; expose a norn stream as tokio
  `AsyncRead + AsyncWrite` and a listener as an accept loop, so
  `axum::serve(norn_listener, app)` and hyper-over-norn "just work"
  (the clean in-process counterpart to FEAT-018). Bridge norn's event loop to
  the host runtime.

## Acceptance Criteria

1. A Rust example dials a peer by pubkey and serves an axum app over a norn
   stream (no TCP listener, no TLS).
2. The crate builds against the installed `libnorn` and round-trips a stream.
3. Binding builds in CI on Linux + macOS.

## Implementation Status

- ✅ `norn-sys` raw FFI crate (hand-written; `build.rs` links via
  `NORN_LIB_DIR` or `pkg-config`).
- ✅ `norn` safe crate: `Keypair`, `Client` (new/id/bootstrap/tick/fd),
  `Stream` (`std::io::Read`+`Write`, and tokio `AsyncRead`+`AsyncWrite` behind
  the `tokio` feature), and `Pump` (FEAT-018 splice engine with Rust
  `Endpoint` closures). `cargo test` 4/4, clippy clean.
- ⏳ Safe `dial(pubkey)`/`listen` wrappers over the async session API, and the
  end-to-end axum-over-norn example (acceptance #1) — next increment.
- ⏳ CI wiring for the crate on Linux + macOS (acceptance #3).

## Cross-repo

Enables thunder/mimir/regin/dvalin (Rust) to consume norn.
