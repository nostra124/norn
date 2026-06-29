---
id: FEAT-038
type: feature
priority: high
complexity: S
estimate_tokens: 3k-6k
estimate_time: 10-20min
phase: planned
status: done
depends_on: ~
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# FEAT-038 — Dedicated Ed25519 node identity (no SSH key dependency)

## Description

**As a** node operator
**I want** nornd to use its own dedicated Ed25519 keypair
**So that** the norn identity is independent of SSH keys and works even
when `~/.ssh/id_ed25519` is absent.

## Changes

### 1. Default identity path

Changed from `~/.ssh/id_ed25519` to `$XDG_CONFIG_HOME/norn/identity`
(falling back to `~/.config/norn/identity`, then `/var/lib/nornd/identity`).

### 2. Auto-generation on first start

If no identity file exists at startup, `auto_generate_identity()` creates a
new Ed25519 keypair via `crypto_keypair_new()` and saves it as a raw
96-byte binary via `crypto_keypair_save()`. No more `ssh-keygen` invocation
or OpenSSH format parsing.

### 3. Identity loading via `crypto_keypair_load()`

Replaced `nornd_identity_load_file()` and `nornd_parse_openssh()` with
`crypto_keypair_load()` from the crypto module, which reads the raw binary
keypair format.

### 4. Removed SSH .pub publication

The SSH public key is no longer published to the fleet key directory.

## Files

- `src/nornd/main.c` — `default_identity()`, `auto_generate_identity()`,
  identity loading via `crypto_keypair_load()`
- `src/libnorn/crypto.h` / `crypto.c` — `crypto_keypair_new()`,
  `crypto_keypair_save()`, `crypto_keypair_load()` (pre-existing)

## Tests

- `tests/test_nornd_identity.c` — `test_default_identity_path`,
  `test_auto_generate_identity`, `test_load_generated_identity`
- Manual: remove `~/.config/norn/identity`, start nornd, verify new key
  is generated at the correct path
