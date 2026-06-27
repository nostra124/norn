# v0.14.0 — Stabilization (Beta): Bug Bash

The second **bug-capture** milestone before 1.0. Fix everything v0.13.0 (Alpha)
surfaced, then capture the deeper defects that only appear under wider, longer,
and adversarial use. Still no new features.

## Status

**PLANNED** — gated on **v0.13.0** (Alpha) exit (no open `critical` bugs; all
catch-up milestones done).

## Purpose

Make it boring. Beta burns down the Alpha bug backlog to zero `critical`/`high`,
then stresses the system in ways normal use won't — soak, fuzzing, fault
injection, security review — capturing and fixing whatever falls out, so v1.0.0
can freeze the API with confidence.

## Bug Tracking

Same scheme as Alpha: `BUG-NNN` under `issues/bug/`, flowing `open/ → build/ →
test/ → done/`. Beta both **closes** the Alpha-filed bugs and **files** new ones
from the wider test surface.

| ID | Title | Severity | Component | Phase |
|----|-------|----------|-----------|-------|
| _(carried from Alpha + captured during Beta)_ | | | | |

## Activities

- **Burn down Alpha bugs:** drive open `critical`/`high` to zero; fix `medium`
  where cheap.
- **Widen the test surface (capture deeper bugs):**
  - Longevity / soak: multi-day multi-node cluster + private overlay under churn.
  - Fuzzing of wire codecs: bencode, IPC request/response, idexch, bep44 records,
    the nornd peer framer, node-served stream protocol.
  - Fault injection: dropped/duplicated/reordered frames, peer churn, disk-full,
    socket errors, malformed identity files.
  - Security review: key handling (file perms, ssh-agent), handshake/auth paths,
    untrusted-input parsers; consider sanitizer (ASan/UBSan) CI runs.
  - Performance baselines: cluster commit latency, key-directory resolve, throughput.
- **Validate packaging on clean hosts:** Debian (.deb install/remove/upgrade),
  Homebrew formula, systemd + launchd unit + socket activation.

## Acceptance Criteria (exit to v1.0.0)

1. **Zero open `critical` and `high` bugs;** `medium`/`low` explicitly deferred
   with rationale.
2. Docs and man pages (norn.1, nornd.8, libnorn.3) complete and accurate.
3. Packaging validated end-to-end on clean hosts; `make distcheck` clean.
4. No regressions across **two** consecutive soak cycles.
5. Security review completed with no unresolved findings.

## Cross-Repo

- All consumers (regin, dvalin, raven, wyrd, thunder, mimir) running on Beta
  builds without `critical`/`high` defects.

## Related Milestones

- **v0.13.0** — Stabilization (Alpha): supplies the initial bug backlog
- **v1.0.0** — GA (tag + API freeze) once Beta exit criteria are met
