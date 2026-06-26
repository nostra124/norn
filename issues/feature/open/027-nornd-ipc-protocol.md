---
id: FEAT-027
type: feature
priority: medium
complexity: M
estimate_tokens: 60k-120k
estimate_time: 90-180min
phase: planned
status: open
depends_on: []
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# IPC protocol codec — length-prefixed bencode request/response

## Description

**As a** `norn` CLI talking to `nornd`
**I want** a small, binary-safe request/response wire format
**So that** the CLI and daemon exchange KV operations over a local socket
without ambiguity or a new dependency.

## Implementation

- `src/nornd/ipc.{c,h}`: a pure codec — no sockets.
- Frame = 4-byte big-endian length + a bencoded dict (reuse libnorn's
  `bencode.c`).
- **Request**: `{ "op": <verb>, ... }` where verb ∈ {put, get, del, cas, watch,
  members, leader, status, keys, ...}; args by key (`key`, `val`, `expect`,
  `prefix`, `id`). Binary-safe values.
- **Response**: `{ "ok": 0|1, "val": <bytes>?, "err": <str>?, "items": [...]? }`.
- Encode/decode helpers that validate structure and bounds; reject malformed or
  oversized frames.
- A typed request/response struct so the daemon and CLI don't poke at bencode
  directly.

## Acceptance Criteria

1. Round-trips every request/response shape (put/get/del/cas/watch/members/
   leader/status) through encode→decode unchanged, including binary values.
2. Rejects truncated, oversized, and malformed frames without reading OOB.
3. Pure module: unit-tested in `src/nornd/` to 100% line + branch coverage
   (nornd's own test, not libnorn's coverage-tracked set).

## Cross-repo

The wire contract any future non-C `norn` client (Rust, scripts) speaks to
nornd.
