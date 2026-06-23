# norn Development Roadmap

## Completed Milestones

### ✅ v0.2.0 — Core Stability
**Status:** DONE (2026-06-23)

| Ticket | Description | Result |
|--------|-------------|--------|
| FEAT-001 | Unit Test Coverage | 84% (16/19 modules) |
| FEAT-002 | Logging Module | done |
| FEAT-012 | Async API | done |

### ✅ v0.3.0 — Documentation
**Status:** DONE (2026-06-23)

| Ticket | Description | Deliverable |
|--------|-------------|-------------|
| FEAT-003 | Architecture Documentation | docs/architecture.md |
| FEAT-004 | API Reference Documentation | Doxygen in headers |
| FEAT-005 | CONTRIBUTING Guide | CONTRIBUTING.md |

### ✅ v0.4.0 — Code Quality
**Status:** DONE (2026-06-23)

| Ticket | Description | Result |
|--------|-------------|--------|
| FEAT-006 | Split norn.c | Decided against (963 lines acceptable) |
| FEAT-007 | Build System Cleanup | -Werror, header install |

### ✅ v0.5.0 — Infrastructure
**Status:** DONE (2026-06-23)

| Ticket | Description | Deliverable |
|--------|-------------|-------------|
| FEAT-008 | CI/CD Pipeline | .github/workflows/ci.yml |
| FEAT-009 | Static Analysis | check-cppcheck, check-tidy |

### ✅ v0.6.0 — User Experience
**Status:** DONE (2026-06-23)

| Ticket | Description | Deliverable |
|--------|-------------|-------------|
| FEAT-010 | CLI Implementation | src/norn.c (keygen, get, set, daemon) |
| FEAT-011 | Man Page | man/norn.1 |

---

## Planned Milestones

### 🔄 v0.7.0 — Multi-Consumer Foundation
**Status:** PLANNED

These tickets enable norn to serve multiple sister projects (bifrost, wyrd) with different crypto/identity requirements.

| Ticket | Description | Priority | Depends On |
|--------|-------------|----------|------------|
| FEAT-013 | Pluggable crypto suite vtable | high | — |
| FEAT-014 | Parameterise Kademlia ID width | high | FEAT-013 |
| FEAT-015 | De-application-ise idexch | high | FEAT-013 |

**Key Changes:**
- `norn_crypto_suite_t` vtable for pluggable crypto (Ed25519, secp256k1, etc.)
- Generic Kademlia routing (20-byte, 32-byte, or custom ID widths)
- Application-agnostic identity exchange

**Consumers:**
- bifrost FEAT-079 (sodium suite)
- wyrd FEAT-291 (secp256k1/ChaCha20 suite)

### 🔄 v0.8.0 — Dial & Session Orchestration
**Status:** PLANNED

| Ticket | Description | Priority | Depends On |
|--------|-------------|----------|------------|
| FEAT-016 | norn_dial(pubkey) → session | high | FEAT-013, FEAT-015 |
| FEAT-017 | Harmonised NAT traversal | high | FEAT-013, FEAT-016 |

**Key Features:**
- `norn_dial(client, pubkey)` — connect by public key, not IP
- `norn_listen()` / accept for inbound sessions
- NAT traversal: rendezvous hole-punch + onion relay fallback

**Consumers:**
- bifrost FEAT-080 (session/sio retirement)
- wyrd FEAT-292

### 🔄 v0.9.0 — Tunnel & Bindings
**Status:** PLANNED

| Ticket | Description | Priority | Depends On |
|--------|-------------|----------|------------|
| FEAT-018 | Stream-tunnel utility (norn-forward) | medium | FEAT-016 |
| FEAT-019 | Language bindings (Rust, Python) | medium | FEAT-016 |

**Key Features:**
- `norn-forward` — TCP/Unix service over norn stream (ssh -L/-R equivalent)
- Rust crate with tokio AsyncRead/AsyncWrite
- Python cffi binding

**Consumers:**
- thunder, mimir, regin, dvalin (Rust)
- raven (Python)

### 🔄 v0.10.0 — Private Overlay
**Status:** PLANNED

| Ticket | Description | Priority | Depends On |
|--------|-------------|----------|------------|
| FEAT-020 | Private overlay bootstrap | medium | FEAT-014, FEAT-016 |

**Key Features:**
- Private mesh formation from fleet bootstrap nodes
- No public mainline DHT announcement
- Rendezvous/relay inside private overlay

**Consumers:**
- regin → dvalin → raven agent fleet
- wyrd private packs/clans

---

## Dependency Graph

```
v0.7.0 (Crypto Foundation)
├── FEAT-013: Crypto suite vtable
├── FEAT-014: Kademlia ID width ──┐
└── FEAT-015: idexch de-app ─────┤
                                  │
v0.8.0 (Dial & Session)          │
├── FEAT-016: norn_dial ──────────┤
└── FEAT-017: NAT traversal ─────┤
                                  │
v0.9.0 (Tunnel & Bindings)        │
├── FEAT-018: norn-forward ───────┤
└── FEAT-019: Language bindings ─┤
                                  │
v0.10.0 (Private Overlay)         │
└── FEAT-020: Private bootstrap ──┘
```

---

## Statistics

| Metric | Value |
|--------|-------|
| Completed Milestones | 5 (v0.2.0–v0.6.0) |
| Planned Milestones | 4 (v0.7.0–v0.10.0) |
| Completed Tickets | 12 |
| Planned Tickets | 8 |
| Version | 0.6.0 |