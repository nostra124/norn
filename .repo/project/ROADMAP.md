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

### ✅ v0.7.0 — Multi-Consumer Foundation
**Status:** DONE (2026-06-24)

These tickets enable norn to serve multiple sister projects (bifrost, wyrd) with different crypto/identity requirements.

| Ticket | Description | Priority | Depends On | Status |
|--------|-------------|----------|-----------|--------|
| FEAT-013 | Pluggable crypto suite vtable | high | — | done |
| FEAT-014 | Parameterise Kademlia ID width | high | FEAT-013 | done |
| FEAT-015 | De-application-ise idexch | high | FEAT-013 | done |

**Key Changes:**
- `norn_crypto_suite_t` vtable for pluggable crypto (Ed25519, secp256k1, etc.)
- Generic Kademlia routing (20-byte, 32-byte, or custom ID widths)
- Application-agnostic identity exchange (`norn_idexch`)

**Consumers:**
- bifrost FEAT-079 (sodium suite)
- wyrd FEAT-291 (secp256k1/ChaCha20 suite)

### ✅ v0.8.0 — Dial & Session Orchestration
**Status:** DONE (2026-06-24) - FEAT-021/022 remaining

| Ticket | Description | Priority | Depends On | Status |
|--------|-------------|----------|-----------|--------|
| FEAT-016 | norn_dial(pubkey) → session | high | FEAT-013, FEAT-015 | done |
| FEAT-017 | Harmonised NAT traversal | high | FEAT-013, FEAT-016 | **partial** |

**Key Features:**
- `norn_dial(client, pubkey)` — connect by public key, not IP
- `norn_listen()` / accept for inbound sessions
- NAT traversal: rendezvous hole-punch + static relay fallback

**Implementation Status:**
- Phase 1: Endpoint discovery ✅
- Phase 2: Direct connection ✅
- Phase 3: Hole punch wire protocol ✅
- Phase 3: Hole punch integration ✅ (FEAT-023 DONE)
- Phase 4: Relay wire protocol ✅
- Phase 4: Relay path integration ❌ (FEAT-022)
- Phase 5: Connection ladder ✅
- UPnP/NAT-PMP ❌ (FEAT-021)

**Remaining Work:**
- FEAT-021: UPnP/NAT-PMP implementation (optional enhancement)
- FEAT-022: Multi-hop relay path integration (optional enhancement)

**Note:** Core NAT traversal (hole punch) is complete. FEAT-021/022 are optional enhancements for improved NAT traversal in restrictive environments.

**Consumers:**
- bifrost FEAT-080 (session/sio retirement)
- wyrd FEAT-292

### 🔄 v0.9.0 — Tunnel & Bindings
**Status:** PLANNED

| Ticket | Description | Priority | Depends On |
|--------|-------------|----------|------------|
| FEAT-018 | Stream-tunnel utility (norn-forward) | medium | FEAT-016 |
| FEAT-019 | Language binding (Rust crate) | medium | FEAT-016 |

**Key Features:**
- `norn-forward` — TCP/Unix service over norn stream (ssh -L/-R equivalent)
- Rust crate with tokio AsyncRead/AsyncWrite
- (Python binding dropped — not needed for the foreseeable future)

**Consumers:**
- thunder, mimir, regin, dvalin (Rust)

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

### 🔄 v0.11.0 — Clustered Key-Value Store (class-aware Raft)
**Status:** DESIGN MERGED (PR #2) — see `issues/MILESTONE-0.11.0-CLUSTER-KV.md`

"etcd over libnorn": a replicated KV store shared across a cluster of nodes,
addressed by public key, that tolerates mostly-offline edge members.

| Ticket | Description | Priority | Depends On |
|--------|-------------|----------|------------|
| FEAT-024 | Pure Raft consensus core (`norn_raft`) — PreVote, learners, candidacy hook | medium | — |
| FEAT-025 | Cluster ↔ session glue (`norn_cluster`) — Raft RPC over norn streams | medium | FEAT-024, FEAT-016, FEAT-017 |
| FEAT-026 | Replicated KV state machine (`norn_kvstore`) + class-aware membership API | medium | FEAT-024, FEAT-025 |

**Key Features:**
- Heterogeneous membership: servers are voting members (quorum over servers
  only), phones/laptops/workstations are learners with a full replica
- Leadership restricted to proven-uptime servers via a candidacy-eligibility
  predicate + PreVote — no change to Raft's safety core
- Learner-first joins; single-server membership changes
- KV ops: put/get/cas/del/watch; linearizable ReadIndex reads; snapshots

**Consumers:**
- regin (agent registry), thunder (shared config), dvalin/raven (state)

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
                                  │
v0.11.0 (Clustered KV Store)      │
├── FEAT-024: Raft core ──────────┤
├── FEAT-025: Cluster/session glue┤
└── FEAT-026: KV + class membership┘
```

---

## Statistics

| Metric | Value |
|--------|-------|
| Completed Milestones | 7 (v0.2.0–v0.8.0) |
| Planned Milestones | 3 (v0.9.0–v0.11.0) |
| Completed Tickets | 17 |
| Planned Tickets | 8 |
| Version | 0.9.0-dev |