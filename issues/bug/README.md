# Bug tickets

Defects are tracked as `BUG-NNN`, mirroring the `issues/feature/` layout. Per
`.repo/project/profile.md`, **bug** tickets flow through these phase directories
(bugs skip the `design/` phase that features have):

```
issues/bug/open/BUG-NNN-short-slug.md     # filed, triaged, not yet started
issues/bug/build/BUG-NNN-short-slug.md    # fix in progress
issues/bug/test/BUG-NNN-short-slug.md     # fix landed, under verification
issues/bug/done/BUG-NNN-short-slug.md     # verified resolved
```

A ticket moves forward by `git mv`-ing the file between phase dirs.

Primarily populated during the **v0.13.0** (Stabilization / Alpha) and
**v0.14.0** (Stabilization / Beta) bug-bash milestones — see
`.repo/project/issues/MILESTONE-0.13.0.md` and `MILESTONE-0.14.0.md` — but a bug
found at any time gets a ticket here.

## Ticket format

```markdown
# BUG-NNN — <one-line title>

- **Severity:** critical | high | medium | low
- **Component:** libnorn/<module> | nornd | norn (CLI) | packaging | docs
- **Found in:** <version / commit / milestone>
- **Target gate:** v0.13.0 | v0.14.0 | v1.0.0

## Repro
<minimal steps / failing test>

## Expected vs actual

## Notes / fix
```

## Severity guide

- **critical** — data loss, crash, security hole, consensus/safety violation;
  blocks the current gate.
- **high** — major feature broken with no workaround; blocks Beta (v0.14.0) exit.
- **medium** — broken with a workaround, or an edge case.
- **low** — cosmetic / minor.
