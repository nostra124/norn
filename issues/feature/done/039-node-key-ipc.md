---
id: FEAT-039
type: feature
priority: medium
complexity: S
estimate_tokens: 2k-4k
estimate_time: 5-10min
phase: planned
status: done
depends_on: FEAT-038
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# FEAT-039 — Node key IPC queries (norn node public|secret)

## Description

**As a** node operator
**I want** to query nornd for its public key and secret key via IPC
**So that** I can inspect the node's identity without direct file access.

## Changes

### 1. nornd IPC handlers

In `main.c:serve_client()`, added two new opcodes handled before the
cluster dispatch:

- `"node-public"` — returns the Ed25519 public key (32 bytes hex-encoded)
  from the in-memory `keypair_t`
- `"node-secret"` — returns the Ed25519 secret/seed (32 bytes hex-encoded)

### 2. CLI verbs

In `src/norn.c`:

- `norn node public` — sends `"node-public"` IPC request to nornd, prints
  hex-encoded public key
- `norn node secret` — sends `"node-secret"` IPC request to nornd, prints
  hex-encoded secret key

Both use the existing `norn_connect_ipc()` / `norn_ipc_send()` plumbing.

## Files

- `src/nornd/main.c` — `serve_client()` handlers for `node-public` /
  `node-secret`
- `src/norn.c` — `norn node public|secret` verb dispatch

## Tests

- Manual: `norn node public` returns hex key; `norn node secret` returns
  hex key; both fail gracefully (with error message) when nornd is not
  running.
