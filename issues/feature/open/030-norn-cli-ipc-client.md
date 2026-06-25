---
id: FEAT-030
type: feature
priority: medium
complexity: M
estimate_tokens: 60k-120k
estimate_time: 90-180min
phase: planned
status: open
depends_on: [FEAT-027, FEAT-029]
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# `norn` CLI refactor — thin IPC client, namespaced verbs

## Description

**As a** user
**I want** `norn` to be a small client that talks to `nornd`
**So that** I drive the cluster KV store (and the fleet key directory) from the
shell without each command spinning up its own node.

## Implementation

- Refactor `src/norn.c` into namespaced subcommands:
  - `norn cluster put <k> <v>` / `get <k>` / `del <k>` / `cas <k> <expect> <v>`
    / `watch <prefix>` / `members` / `leader` / `status` — each connects to the
    nornd socket (FEAT-027 codec), sends one request, prints the reply, exits.
    `watch` stays connected and prints streamed events.
  - `norn bep44 get <k>` / `set <k> <v>` — today's direct-DHT BEP-44 record
    commands, unchanged (move under the `bep44` namespace).
  - `norn keys <id>` / `authorized-keys` — read the key directory (FEAT-031).
  - `norn keygen` / `version` — local, no daemon.
- Socket path resolution (`NORN_SOCK` → `$XDG_RUNTIME_DIR/nornd.sock` →
  `~/.norn/nornd.sock`); friendly error if nornd isn't running.
- Output formats suitable for scripting (raw value on stdout for `get`; tabular
  for `members`/`status`).

## Acceptance Criteria

1. `norn cluster put k v` then `norn cluster get k` prints `v`, round-tripping
   through nornd.
2. `norn cluster watch p` prints a line per change under prefix `p`.
3. `norn bep44 …` retains current behaviour; `keygen`/`version` work with no
   daemon; a missing daemon yields a clear error and non-zero exit.
4. Man page / `--help` updated for the namespaced verbs.

## Cross-repo

The day-to-day interface to a fleet's shared state.
