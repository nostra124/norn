---
id: FEAT-041
type: feature
priority: high
complexity: M
estimate_tokens: 5k-10k
estimate_time: 20-40min
phase: planned
status: done
depends_on: ~
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# FEAT-041 — CLI verb rework: `norn node` and `norn peer` command groups

## Description

**As a** CLI user
**I want** a clean, discoverable command hierarchy
**So that** the help output shows only direct children and commands are
organized by domain (daemon management vs. peer interaction).

## Changes

### 1. New command groups

**`norn node`** — manage the local nornd daemon:
- `norn node start` — start nornd (stub, suggests systemd)
- `norn node restart` — restart nornd (stub)
- `norn node status` — IPC ping to nornd, reports running/stopped
- `norn node public` — print node's Ed25519 public key (IPC to nornd)
- `norn node secret` — print node's Ed25519 secret key (IPC to nornd)

**`norn peer`** — interact with remote peers:
- `norn peer list` — list known peers from DHT routing table (IPC to nornd)
- `norn peer connect <pubkey[@h:p]>` — dial a remote peer
- `norn peer get <pubkey[@h:p]> <key>` — served-KV get from a peer
- `norn peer cat <pubkey[@h:p]> <hash>` — served-KV cat by SHA-256 hash

### 2. Help shows direct children only

`norn --help` lists only top-level command groups (node, peer, bep44,
cluster, keys, keygen, version) with brief descriptions. Sub-commands are
documented via `norn node --help` / `norn peer --help` etc.

### 3. Backward-compatible aliases (hidden from help)

`norn get`, `norn set`, `norn daemon` still work as undocumented aliases.

### 4. File renames

`src/nornd/cli_node.c` → `src/nornd/cli_peer.c` (served-KV dial code,
renamed from `nornd_cli_node` to `nornd_cli_peer`).

### 5. New IPC ping handler

Added `"ping"` → `"pong"` handler in nornd's `serve_client()` so
`norn node status` can health-check the daemon.

## Files

- `src/norn.c` — new `usage()`, `do_node_start/restart/status()`,
  `do_peer()` dispatch, updated `main()` dispatch
- `src/nornd/cli_peer.c` — renamed from `cli_node.c`, function renamed to
  `nornd_cli_peer`
- `src/nornd/cli_peer.h` — renamed from `cli_node.h`
- `src/nornd/main.c` — added `ping` IPC handler
- `Makefile.am` — updated references

## Tests

- Manual: `norn --help` shows direct children; `norn node --help` shows
  node subcommands; `norn peer --help` shows peer subcommands;
  `norn version` still works; `norn node status` returns error gracefully
  when nornd is not running.
