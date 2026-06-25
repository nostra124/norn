---
id: FEAT-031
type: feature
priority: medium
complexity: M
estimate_tokens: 60k-120k
estimate_time: 90-180min
phase: planned
status: open
depends_on: [FEAT-029, FEAT-028]
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# Fleet key directory — publish/resolve SSH + GPG public keys

## Description

**As a** fleet
**I want** every node's SSH and GPG **public** keys shared in the cluster
**So that** any member can resolve another's keys — a distributed
`authorized_keys` + GPG keyring, keyed off the SSH identity we already trust.

## Implementation

- `src/nornd/keydir.{c,h}` over `norn_cluster_kv_*`:
  - On startup nornd reads its local SSH public key (`~/.ssh/id_ed25519.pub`)
    and GPG public key (`gpg --export --armor <self>`), and `kv_put`s them under
    well-known keys:
    - `peer/<nodeid>/ssh` → `ssh-ed25519 AAAA…`
    - `peer/<nodeid>/gpg` → ASCII-armored GPG public key
    where `<nodeid>` is the node's pubkey (hex). They replicate to all members.
  - Re-publish on change / periodically; GPG keys are opaque KV values (norn
    does no GPG crypto).
  - Resolve helpers: get a peer's ssh/gpg key; enumerate `peer/*` for
    `authorized-keys`.
- CLI surface (FEAT-030):
  - `norn keys <nodeid>` — print a peer's SSH + GPG keys.
  - `norn authorized-keys` — emit an `authorized_keys` file assembled from every
    `peer/*/ssh` in the cluster (optionally with `from=`/comment annotations).

## Acceptance Criteria

1. After a node joins, its `peer/<id>/ssh` and `peer/<id>/gpg` are present on
   every member.
2. `norn keys <id>` resolves a peer's keys; `norn authorized-keys` produces a
   valid `authorized_keys` file covering the live fleet.
3. GPG keys round-trip as opaque values (armored block in, identical out).

## Cross-repo

A turnkey fleet PKI directory: SSH login trust and GPG verification/encryption
distributed over the same cluster that already holds shared config/state.
