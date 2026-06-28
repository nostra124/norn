---
id: FEAT-035
type: feature
priority: medium
complexity: M
estimate_tokens: 4k-8k
estimate_time: 10-20min
phase: planned
status: done
depends_on: [FEAT-034]
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# FEAT-035 — DHT node and peer cache persistence for nornd

## Description

**As a** nornd operator
**I want** the DHT routing table and peer cache to be saved to disk periodically
and reloaded on startup
**So that** the node re-joins the DHT quickly after a restart instead of
re-bootstrapping from scratch.

## Implementation

### Public API (libnorn)

Added four new functions to `norn.h` / `norn_impl.c` that wrap the existing
internal `mainline_save_nodes` / `mainline_load_nodes` and
`peer_cache_save` / `peer_cache_load`:

- `int norn_save_dht_nodes(norn_client_t *client, const char *path)`
- `int norn_load_dht_nodes(norn_client_t *client, const char *path)`
- `int norn_save_peer_cache(norn_client_t *client, const char *path)`
- `int norn_load_peer_cache(norn_client_t *client, const char *path)`

### Persistence in nornd (`src/nornd/main.c`)

- After `norn_new()` + `nornd_transport_new()`, call `norn_load_dht_nodes()`
  and `norn_load_peer_cache()` to restore the previous DHT state.
- In the event loop (every ~300s), call `norn_save_dht_nodes()` and
  `norn_save_peer_cache()` to persist the current state.
- On shutdown (before `norn_free()`), save one final time so the most
  recent node set is available on next start.
- Files are stored under the data directory (`--data-dir` or `~/.norn/`):
  - `dht_nodes` — binary routing table dump
  - `peer_cache` — binary peer cache dump

## Files

- `src/libnorn/norn.h` — declarations for `norn_save/load_dht_nodes`,
  `norn_save/load_peer_cache`
- `src/libnorn/norn_impl.c` — implementations
- `src/nornd/main.c` — load on startup, periodic save, final save on shutdown

## Tests

- `tests/test_norn.c` — `test_save_load_dht_nodes` (round-trip),
  `test_save_load_peer_cache` (round-trip), null-safety
- `tests/sit/nornd.bats` — verify dht_nodes and peer_cache files exist
  in data directory after nornd runs
