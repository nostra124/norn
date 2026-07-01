# BUG-003: `norn peer connect` always fails

**Status**: fixed  
**Component**: `src/nornd/peer_fetch.c`  
**Introduced in**: FEAT-012 (peer-fetch IPC op)  

## Symptoms

`norn peer connect <pubkey>` exits with status 1 and prints `norn: bad served-KV request` immediately, regardless of whether the peer spec is valid.

## Root Cause

The `nornd_peer_fetch()` function sets `sv_verb = GET` and `arg = ""` for the `connect` verb, then unconditionally calls `nornd_served_encode_req(GET, "", ...)`, which returns -1 because GET requires a non-empty argument. The `connect` verb never reaches the dial code.

## Fix

Move `just_dial` determination before request-line encoding and skip encoding entirely when `just_dial` is set. The request line is only needed for `get`/`cat`/`list` verbs.

## Tests

- Unit test: `test_nornd_peer_fetch.c` validates that the `connect` fix doesn't change error behavior for other paths.
- SIT: `cli.bats` tests `norn peer connect dead` and verifies it fails with the correct spec-validation error (not `bad served-KV request`).
