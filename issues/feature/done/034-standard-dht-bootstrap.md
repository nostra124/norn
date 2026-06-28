---
id: FEAT-034
type: feature
priority: medium
complexity: S
estimate_tokens: 2k-5k
estimate_time: 5-15min
phase: planned
status: done
depends_on: ~
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# FEAT-034 — Standard DHT bootstrap in libnorn

## Description

**As a** developer using libnorn
**I want** every client to automatically bootstrap to the Mainline DHT
**So that** applications don't need explicit `norn_bootstrap()` calls and
user-mode nornd instances can discover the system daemon.

## Changes

### 1. Auto-bootstrap in `norn_listen_async()`

`norn_listen_async()` now calls `norn_bootstrap()` after binding the UDP
socket. Every application that calls `norn_listen_async()` automatically
joins the DHT without an explicit bootstrap call. The redundant explicit
call was removed from `src/nornd/main.c`.

- `src/libnorn/norn_session.c:506` — `norn_listen_async()` → `norn_bootstrap(client)`

### 2. Local bootstrap node (127.0.0.1:6881)

`mainline_bootstrap()` always sends a `find_node` to 127.0.0.1:6881 first.
This lets user-mode nornd instances discover the system daemon running on
the well-known port. The system daemon's self-query is harmless.

- `src/libnorn/norn.c:324` — added 127.0.0.1:6881 as first bootstrap target

### 3. Default well-known port for nornd

`nornd --listen-port` default changed from `0` (OS ephemeral) to `6881`
(the standard Mainline DHT port), so the system daemon always binds a
consistent, well-known port.

- `src/nornd/main.c:391` — `listen_port = 6881`

### 4. Config knob for local bootstrap port

Added `local_port` field to `norn_config_t` (0 = default 6881) so
applications can override the local bootstrap port.

- `src/libnorn/norn.h:110` — `uint16_t local_port`

## Files

- `src/libnorn/norn.h` — `norn_config_t.local_port`
- `src/libnorn/norn_session.c` — auto-bootstrap in `norn_listen_async()`
- `src/libnorn/norn.c` — local bootstrap in `mainline_bootstrap()`
- `src/nornd/main.c` — default port 6881, removed redundant bootstrap call

## Tests

- `tests/test_mainline.c` — `test_local_bootstrap` verifies 127.0.0.1:6881
  is added first; `test_bootstrap_order` verifies local→private→public order
- `tests/test_norn.c` — `test_listen_async_bootstrap` verifies
  `norn_bootstrap()` is called from `norn_listen_async()`
- `tests/test_norn_session.c` — `test_listen_async_port_zero` verifies
  port 0 (ephemeral) still works
- `tests/sit/nornd.bats` — verify nornd defaults to port 6881
