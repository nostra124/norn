# norn Development Roadmap

> **Ordering discipline (post-jump correction).** Milestones are sequential.
> Work jumped ahead — v0.9 → v0.11 → v0.12 — while **v0.10.0 (Private Overlay)
> was never completed** and several v0.8/v0.12 tails were left open. We do **not**
> open a new milestone until the current one is genuinely closed. The
> **Catch-up Backlog** below is the ordered debt to clear before the Alpha/Beta
> stabilization track and 1.0. No feature work skips ahead of it.

## Completed Milestones

### ✅ v0.2.0 — Core Stability
**Status:** DONE (2026-06-23)

| Ticket | Description | Result |
|--------|-------------|--------|
| FEAT-001 | Unit Test Coverage | now 100% line+branch on all coverage-tracked sources; **gate blocking** |
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
| FEAT-009 | Static Analysis | cppcheck (libnorn **+ nornd**) + clang-tidy |

### ✅ v0.6.0 — User Experience
**Status:** DONE (2026-06-23)

| Ticket | Description | Deliverable |
|--------|-------------|-------------|
| FEAT-010 | CLI Implementation | src/norn.c (keygen, get, set, daemon) |
| FEAT-011 | Man Page | man/norn.1 (+ nornd.8, libnorn.3) |

### ✅ v0.7.0 — Multi-Consumer Foundation
**Status:** DONE (2026-06-24)

| Ticket | Description | Priority | Depends On | Status |
|--------|-------------|----------|-----------|--------|
| FEAT-013 | Pluggable crypto suite vtable | high | — | done |
| FEAT-014 | Parameterise Kademlia ID width | high | FEAT-013 | done |
| FEAT-015 | De-application-ise idexch | high | FEAT-013 | done |

### 🟢 v0.8.0 — Dial & Session Orchestration
**Status:** CORE DONE (2026-06-24) — optional tails **deferred post-1.0**

| Ticket | Description | Priority | Depends On | Status |
|--------|-------------|----------|-----------|--------|
| FEAT-016 | norn_dial(pubkey) → session | high | FEAT-013, FEAT-015 | done |
| FEAT-017 | Harmonised NAT traversal | high | FEAT-013, FEAT-016 | core done (hole-punch) |
| FEAT-021 | UPnP/NAT-PMP automatic port forwarding | low | FEAT-017 | **deferred → post-1.0** |
| FEAT-022 | Multi-hop relay path integration | low | FEAT-017 | **deferred → post-1.0** |

Core dial + rendezvous hole-punch (FEAT-023) are complete. FEAT-021/022 are
optional NAT-traversal enhancements for restrictive networks and are **formally
deferred past 1.0** (see Catch-up Backlog) — they need real router / multi-hop
topology and are PIT-only, so they don't gate 1.0.

### ✅ v0.9.0 — Tunnel & Bindings
**Status:** DONE — norn-forward (client+server) + Rust crate (PIT for live two-peer round-trips)

| Ticket | Description | Priority | Depends On | Status |
|--------|-------------|----------|------------|--------|
| FEAT-018 | Stream-tunnel utility (norn-forward) | medium | FEAT-016 | done |
| FEAT-019 | Language binding (Rust crate) | medium | FEAT-016 | done |

### ✅ v0.11.0 — Clustered Key-Value Store (class-aware Raft)
**Status:** DONE — all three features at 100% line+branch coverage

"etcd over libnorn": a replicated KV store shared across a cluster of nodes,
addressed by public key, that tolerates mostly-offline edge members.

| Ticket | Description | Priority | Depends On | Status |
|--------|-------------|----------|------------|--------|
| FEAT-024 | Pure Raft consensus core (`norn_raft`) | medium | — | done (100% cov) |
| FEAT-025 | Cluster ↔ session glue (`norn_cluster`) | medium | FEAT-024, FEAT-016, FEAT-017 | done (100% cov) |
| FEAT-026 | Replicated KV state machine (`norn_kvstore`) + class-aware membership | medium | FEAT-024, FEAT-025 | done (100% cov) |

> **Out-of-order note:** v0.11.0 and v0.12.0 were built *before* v0.10.0. That is
> the ordering break this roadmap is correcting. v0.10.0 must be caught up.

---

## ⚠️ Catch-up Backlog — finish IN ORDER before stabilization

No new milestone opens until these close. Listed earliest-gap-first.

### 1. v0.10.0 — Private Overlay  *(SKIPPED — highest-priority catch-up)*
**Status:** OPEN — config API + docs exist; private-mesh formation unverified.

| Ticket | Description | Depends On | Status |
|--------|-------------|------------|--------|
| FEAT-020 | Private overlay bootstrap | FEAT-014, FEAT-016, FEAT-017 | open |

Closed fleets need pubkey-addressed connectivity with **no public mainline DHT
announce**. `norn_config_t` already carries `private_mode` + `boot_*`; harden it
into a first-class private-overlay story. Acceptance: ≥3 nodes form a private
overlay from one bootstrap node; pubkey resolution via private Kademlia; a NAT'd
member reachable via in-fleet rendezvous/relay; zero public-DHT traffic;
`docs/PRIVATE-OVERLAY.md`. Network validation via PIT. (See
`.repo/project/issues/MILESTONE-0.10.0.md`.)

### 2. v0.8.0 optional tails — **DECISION: deferred to post-1.0**
| Ticket | Description | Status |
|--------|-------------|--------|
| FEAT-021 | UPnP/NAT-PMP automatic port forwarding | **deferred → post-1.0** |
| FEAT-022 | Multi-hop relay path integration | **deferred → post-1.0** |

Both are optional NAT-traversal **enhancements**, not 1.0 blockers — core
traversal (rendezvous hole-punch, FEAT-023) already works. They also can't be
verified deterministically by the unit/CI methodology: FEAT-021 requires a real
UPnP/NAT-PMP gateway, and FEAT-022 requires real multi-hop relay topology (both
PIT-only). To keep the march to the stabilization gates honest rather than ship
untestable router/multi-hop code, both are **formally deferred past 1.0** (no
longer half-open). Tracked in their tickets; revisit after GA.

Consequence: **FEAT-020 acceptance #2** (a NAT'd member reachable via
rendezvous/relay inside the overlay) depends on FEAT-022 and therefore also
defers post-1.0. v0.10.0 ships its **acceptance #1** (overlay formation +
pubkey resolution + no public-DHT pollution, integration-tested) for 1.0.

### 3. v0.12.0 tails (daemon/CLI seams)
The pure cores and single- *and* multi-node operation shipped; these network-bound
seams remain:

| Ticket | Remaining work | Status |
|--------|----------------|--------|
| FEAT-028 | ssh-agent signer (file parser already done) | open |
| FEAT-030 | `watch` event stream + `bep44` verb namespacing | open |
| FEAT-031 | `authorized-keys` enumeration (needs KV prefix-scan) | open |
| FEAT-033 | Node-served KV: file-backed store + peer dial transport (stream codec done) | open |

---

## Stabilization Track → 1.0 (bug capture)

Opens **only after** the Catch-up Backlog is clear and the tree is feature-frozen.
These two milestones add **no features** — their sole job is to surface, triage,
and fix defects. Bugs are filed as `BUG-NNN` under `issues/bug/`, flowing through
`open/ → build/ → test/ → done/`, and burned down before each gate.

### 🅰️ v0.13.0 — Stabilization (Alpha): Bug Bash
**Status:** PLANNED (gated on Catch-up Backlog complete + feature freeze)

Purpose: **capture bugs** via internal dogfooding across the consumer fleet.

- Feature freeze: no new FEAT tickets; only `BUG-NNN` fixes.
- Exercise everything: full `make check`, SIT + PIT, multi-node cluster soak,
  private-overlay formation, key-directory + node-served KV end-to-end.
- Dogfood through consumers (regin, dvalin, raven, wyrd, thunder, mimir).
- Every defect → a `BUG-NNN` ticket with repro; triage by severity.
- **Exit criteria:** all catch-up milestones DONE; no open `critical` bugs; every
  `BUG` triaged with owner + target gate; coverage gate green & blocking.

(See `.repo/project/issues/MILESTONE-0.13.0.md`.)

### 🅱️ v0.14.0 — Stabilization (Beta): Bug Bash
**Status:** PLANNED (gated on v0.13.0 exit)

Purpose: **capture remaining bugs** under wider/longer/adversarial use and fix
everything Alpha surfaced.

- Fix all Alpha-captured bugs; widen testing (longevity/soak, fuzzing of wire
  codecs, fault injection, security review, performance baselines).
- Validate packaging on clean hosts (Debian packages, Homebrew, systemd/launchd).
- **Exit criteria:** zero open `critical`/`high` bugs; docs + man pages complete;
  packaging validated; `distcheck` clean; no regressions across two soak cycles.

(See `.repo/project/issues/MILESTONE-0.14.0.md`.)

### 🎯 v1.0.0 — General Availability
**Status:** PLANNED (gated on Beta exit) — tag the release; freeze the public API.

---

## Dependency Graph

```
v0.7.0 (Crypto Foundation)         [DONE]
├── FEAT-013: Crypto suite vtable
├── FEAT-014: Kademlia ID width ──┐
└── FEAT-015: idexch de-app ──────┤
                                  │
v0.8.0 (Dial & Session)  [CORE DONE; tails deferred post-1.0]
├── FEAT-016: norn_dial ──────────┤
├── FEAT-017: NAT traversal ──────┤
├── FEAT-021: UPnP/NAT-PMP  (deferred post-1.0)
└── FEAT-022: multi-hop relay (deferred post-1.0)
                                  │
v0.9.0 (Tunnel & Bindings) [DONE]  │
├── FEAT-018: norn-forward ───────┤
└── FEAT-019: Language bindings ──┤
                                  │
v0.10.0 (Private Overlay) [OPEN]   │   <-- catch up FIRST (was skipped)
└── FEAT-020: Private bootstrap ──┘
                                  │
v0.11.0 (Clustered KV Store) [DONE]│
├── FEAT-024: Raft core ──────────┤
├── FEAT-025: Cluster/session glue┤
└── FEAT-026: KV + class membership┘
                                  │
v0.12.0 (nornd + norn IPC CLI) [core done; tails open]
├── FEAT-027: IPC bencode codec [done]
├── FEAT-028: SSH identity [file done / agent open]
├── FEAT-029: nornd daemon [done: single + multi-node]
├── FEAT-030: norn CLI client [cluster/keys done / watch+bep44 open]
├── FEAT-031: fleet key directory [done / authorized-keys open]
├── FEAT-033: node-served KV [stream codec done / file store + dial open]
└── FEAT-032: packaging (svc units) [done]
                                  │
Stabilization -> 1.0
├── v0.13.0: Bug Bash (Alpha)    no features — capture bugs
├── v0.14.0: Bug Bash (Beta)     fix + harden
└── v1.0.0:  GA
```

---

## Statistics

| Metric | Value |
|--------|-------|
| Version (VERSION file) | 0.12.0 |
| Fully-completed milestones | 9 (v0.2.0–v0.7.0, v0.9.0, v0.11.0) |
| v0.10.0 (acceptance #1) | ✅ done — overlay forms, integration-tested |
| v0.8.0 tails (FEAT-021/022) | deferred → post-1.0 (optional, PIT-only) |
| Partially-complete (tails open) | 1 (v0.12.0) |
| Planned stabilization milestones | 3 (v0.13.0, v0.14.0, v1.0.0) |
| Coverage (tracked sources) | 100% line+branch, gate blocking |

### Sequence to 1.0 (do not jump)
1. ~~**v0.10.0** Private Overlay (FEAT-020)~~ — ✅ done (acceptance #1; #2 defers with FEAT-022)
2. ~~**v0.8.0** tails (FEAT-021/022)~~ — **decided: deferred post-1.0** (optional, PIT-only)
3. **v0.12.0** tails (FEAT-028/030/031/033) ← **next**
4. **v0.13.0** — Bug Bash (Alpha — capture)
5. **v0.14.0** — Bug Bash (Beta — fix + harden)
6. **v1.0.0** — GA
