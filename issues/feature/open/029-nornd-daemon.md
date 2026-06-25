---
id: FEAT-029
type: feature
priority: medium
complexity: L
estimate_tokens: 120k-240k
estimate_time: 180-360min
phase: planned
status: open
depends_on: [FEAT-027, FEAT-028, FEAT-025]
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# `nornd` daemon — node + cluster host + unix-socket IPC server

## Description

**As a** machine in a norn fleet
**I want** a small daemon that runs a norn node and hosts the cluster KV store
**So that** local tools (the `norn` CLI, apps) drive shared state over a simple
socket while one process owns the event loop.

## Implementation

- `nornd` binary (`src/nornd/main.c` + helpers):
  - Load identity (FEAT-028: SSH key file or ssh-agent).
  - Create the libnorn client; bootstrap public mainline **or** a private
    overlay (FEAT-020) from config/flags.
  - Create a `norn_cluster` (FEAT-025); single-node by default, or
    bootstrap/join configured server peers.
  - One event loop: `poll(norn_fd + unix_listen_fd + client_fds)` driving
    `norn_tick` + `norn_cluster_tick` and servicing IPC connections.
  - **IPC server**: listen on `$XDG_RUNTIME_DIR/nornd.sock` (→ `~/.norn/
    nornd.sock`); per connection, decode a request (FEAT-027), dispatch to
    `norn_cluster_kv_*` / introspection, encode the response. `watch` keeps the
    connection open and streams change frames (via a KV watch callback).
  - Clean shutdown: unlink the socket, free the cluster/client.
- Socket/event-loop is daemon glue (not unit-coverage-tracked); the dispatch
  mapping (request → cluster call → response) is factored so it can be tested
  against a fake cluster.

## Acceptance Criteria

1. `nornd` starts with an SSH identity and serves the Unix socket; a second
   instance refuses a stale/locked socket cleanly.
2. put/get/del/cas requests map to the cluster KV and return correct responses;
   members/leader/status report real cluster state.
3. `watch` streams a frame per matching committed change until the client
   disconnects.
4. Graceful SIGINT/SIGTERM shutdown removes the socket.

## Cross-repo

The reference node daemon the fleet runs; the thing `norn` (FEAT-030) and apps
talk to.
