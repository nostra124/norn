---
id: FEAT-033
type: feature
priority: medium
complexity: L
estimate_tokens: 90k-180k
estimate_time: 150-300min
phase: planned
status: done
depends_on: [FEAT-029, FEAT-016, FEAT-018]
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# Node-served key-value store — direct, streamed, pubkey-addressed content

## Description

**As a** node operator
**I want** my node to serve its own key-values directly to peers on request
**So that** I get a per-node authoritative store with **no replication and no
DHT** — and, because values are **streamed**, I can serve arbitrarily large
objects (a 1 GB file) addressed by my public key.

This is the third KV surface, distinct from the replicated **cluster KV**
(consensus, small values) and the public **BEP-44 DHT** (small public records):
each node is a pubkey-addressed content server.

## Implementation

- **Local served store** (nornd): mutable keys (`set`/`del`/`list`) and
  immutable content-addressed objects (`put` → SHA-256 hash). Large/immutable
  values are backed by files under the data dir, **not** held in RAM.
- **Served-KV protocol over norn streams** (reusing FEAT-018 inbound-stream
  accept): a peer dials this node, opens a stream, sends a request
  (`GET <key>` / `CAT <hash>` / `LIST <prefix>`), and the node **streams** the
  value back (chunked; never buffers the whole object). The verified peer pubkey
  (FEAT-016) is available for per-key authorization hooks.
- **CLI**:
  - `norn node set <k> <v|->` / `put <v|-|@file>` / `list [<prefix>]` /
    `del <k>` — manage the local node's served store (via IPC to own nornd).
  - `norn peer get <nodeid> <k>` / `cat <nodeid> <hash>` — nornd dials the peer
    and streams the reply to stdout.
- **Composition**: publish a blob with `node put` (get its hash), advertise a
  small `blob/<hash> → nodeid` pointer in the **cluster** store, and peers
  `peer cat <nodeid> <hash>` to stream it.

## Acceptance Criteria

1. `norn node put @bigfile` stores a multi-hundred-MB object and prints its
   content hash without buffering it in memory.
2. `norn peer cat <nodeid> <hash>` on another node streams the identical bytes
   to stdout; integrity verified against the content hash.
3. `node set/get/list/del` round-trip mutable served values; `peer get` fetches
   a peer's mutable value.
4. Backpressure works: a slow reader does not force the server to buffer the
   whole object; transfer is length-correct under load.

## Cross-repo

A pubkey-addressed content/file service for the fleet (artifacts, models,
datasets) — the bulk-data counterpart to the small-value cluster KV.
