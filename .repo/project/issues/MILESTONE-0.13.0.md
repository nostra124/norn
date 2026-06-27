# v0.13.0 — Stabilization (Alpha): Bug Bash

A **bug-capture** milestone on the road to 1.0. No new features — the goal is to
surface and triage every defect through internal dogfooding before the
v0.14.0 hardening pass.

## Status

**PLANNED** — gated on the Catch-up Backlog being complete (v0.10.0 + v0.8.0 and
v0.12.0 tails resolved) and a **feature freeze** in effect.

## Purpose

Find bugs. This is the first stabilization gate before 1.0: freeze the feature
set, exercise the whole system the way real consumers will, and file every defect
as a tracked ticket. Fixing is opportunistic here; the deliverable is a triaged,
reproducible bug inventory. (v0.14.0 is where the backlog is burned down and
the system is hardened.)

## Bug Tracking

Defects are filed as `BUG-NNN` under `issues/bug/` and flow through the phase dirs
`open/ → build/ → test/ → done/` (the bug phases per `profile.md`; bugs skip
`design/`). Each ticket carries: title, severity (`critical` / `high` / `medium`
/ `low`), a minimal repro, the affected component, and a target gate.

| ID | Title | Severity | Component | Phase |
|----|-------|----------|-----------|-------|
| _(captured during the bash)_ | | | | |

## Activities

- **Feature freeze:** no new `FEAT-NNN` work merges; only `BUG-NNN` fixes and
  test/doc updates.
- **Exercise everything:**
  - `make check` (full unit suite, gate blocking at 100%).
  - SIT (build/install/CLI/handshake/session/nornd/cluster) and PIT (perf/network).
  - Multi-node cluster soak: ≥3 nornd nodes, replication + leader failover.
  - Private overlay (v0.10.0) formation + NAT'd member reachability.
  - Fleet key directory (SSH/GPG publish/resolve) and node-served KV end-to-end.
  - Packaging smoke: Debian packages, Homebrew, systemd/launchd units on clean hosts.
- **Dogfood through consumers:** regin, dvalin, raven, wyrd, thunder, mimir —
  capture integration defects against the real APIs.
- **Triage:** every defect → a `BUG-NNN` ticket with repro and severity.

## Acceptance Criteria (exit to v0.14.0)

1. All Catch-up Backlog milestones are DONE (v0.10.0; v0.8.0 tails decided;
   v0.12.0 tails closed).
2. Feature freeze held for the milestone's duration.
3. Every observed defect is filed as a `BUG-NNN` with a reproduction and severity.
4. **No open `critical` bugs.** (`high`/`medium`/`low` may carry into Beta with an
   owner and target gate.)
5. Coverage gate remains green and blocking; `distcheck` clean.

## Cross-Repo

- regin / dvalin / raven — agent-fleet integration dogfooding
- wyrd — private packs/clans dogfooding
- thunder / mimir — shared-config / state consumers

## Related Milestones

- **Catch-up Backlog** (v0.10.0, v0.8.0 tails, v0.12.0 tails) — must precede this
- **v0.14.0** — Stabilization (Beta): fixes the bugs this captures and hardens
- **v1.0.0** — GA
