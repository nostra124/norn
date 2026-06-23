# v0.9.0 — Tunnel & Bindings

Service-over-pubkey and non-C consumers.

## Status (2026-06-23)

**PLANNED** — Depends on v0.8.0 (FEAT-016)

## Tickets

| ID | Title | Priority | Depends On | Status |
|----|-------|----------|------------|--------|
| FEAT-018 | Stream-tunnel utility (norn-forward) | medium | FEAT-016 | open |
| FEAT-019 | Language bindings (Rust, Python) | medium | FEAT-016 | open |

## Overview

HTTP and other TCP services can ride norn streams without code changes. This milestone
provides:
1. A generic tunnel utility (`norn-forward`) for TCP/Unix services
2. Language bindings for Rust and Python consumers

## Key Features

### FEAT-018: norn-forward

Lift bifrost's `forward`/`SVC_TUNNEL` into norn as a reusable utility:

**Server side:**
- `listen()` on norn
- Per inbound session, open local TCP/Unix connection to wrapped service
- Pump bytes bidirectionally

**Client side:**
- Listen on local socket
- Forward each connection to `norn_dial(pubkey)`

**Features:**
- Ship as `norn-forward` binary **and** library entry
- TLS dropped (norn already E2E encrypts)
- Verified peer pubkey exposed to wrapped service (env/header hook)

### FEAT-019: Language Bindings

**Rust crate:**
- Safe wrapper over C FFI
- Expose norn stream as tokio `AsyncRead + AsyncWrite`
- Listener as accept loop
- `axum::serve(norn_listener, app)` and hyper-over-norn "just work"

**Python binding:**
- Thin cffi/ctypes wrapper over C SDK
- For raven integration

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Application (thunder HTTP API, mimir, regin, etc.)        │
│                                                             │
│  ┌─────────────────┐     ┌─────────────────────────────┐  │
│  │ Rust crate      │     │ Python binding              │  │
│  │ (tokio Async)   │     │ (cffi)                      │  │
│  └────────┬────────┘     └──────────────┬──────────────┘  │
│           │                               │                │
│           └───────────────┬───────────────┘                │
│                           │                                │
│                    C SDK (libnorn)                         │
│                           │                                │
│  ┌────────────────────────┼────────────────────────────┐  │
│  │ norn_dial(pubkey) → session                          │  │
│  │     ├─→ Stream (TCP-like)                             │  │
│  │     └─→ Datagram (UDP-like)                          │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ norn-forward                                         │   │
│  │  Server: norn listen → TCP connect → pump           │   │
│  │  Client: TCP listen → norn dial → pump              │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Acceptance Criteria

1. `curl http://localhost:PORT` tunnels over norn to remote HTTP server by pubkey
2. Bidirectional byte transfer under load
3. Peer pubkey available to wrapped service
4. Rust example: dial peer, serve axum app over norn stream (no TCP listener, no TLS)
5. Python example: publish/resolve, exchange stream
6. Bindings build in CI on Linux + macOS

## Cross-Repo

- thunder, mimir, regin, dvalin (Rust) — day-1 "HTTP over libnorn"
- raven (Python) — thin binding for agent
- bifrost — can embed norn-forward instead of custom forward logic

## Related Milestones

- **v0.8.0**: Dial & Session (prerequisite)
- **v0.10.0**: Private Overlay (same infrastructure)