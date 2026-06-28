---
id: FEAT-010
type: feature
priority: medium
complexity: L
estimate_tokens: 80k-150k
estimate_time: 90-180min
phase: done
status: done
depends_on: [FEAT-001]
milestone: MILESTONE-0.6.0
spawned_from: ~
---
# CLI Implementation

Implement a functional CLI for DHT operations.

## Description

**As a** norn CLI user
**I want** a working CLI that can get/set records and run a DHT node
**So that** I can interact with the DHT from the command line

Currently `src/norn.c` is a stub that prints placeholder messages. Need full implementation of keygen, get, set, and daemon commands.

## Implementation

### Commands

1. **`norn keygen`** — Generate ed25519 keypair, save to `~/.norn/key.pem`, print pubkey
2. **`norn get <key>`** — Retrieve record from DHT by pubkey
3. **`norn set <key> <value>`** — Store signed record to DHT
4. **`norn daemon`** — Run as DHT node, respond to queries
5. **`norn version`** — Print version

### Options

```
Global options:
  --key <path>           Key file (default: ~/.norn/key.pem)
  --port <port>          DHT port (default: 6881)
  --log-level <level>    Log level: debug, info, warn, error
  --help                 Show help

Get options:
  --timeout <ms>         Query timeout (default: 5000)
  --json                 Output as JSON with metadata

Set options:
  --seq <n>              Sequence number (default: auto-increment)
  --salt <string>        Salt for salted items

Daemon options:
  --read-only            Read-only mode (BEP-43)
  --private              Private overlay mode
  --bootstrap <ip:port>  Add bootstrap peer
```

### Implementation

Full rewrite of `src/norn.c`:
- Parse options with `getopt_long`
- Initialize libsodium
- Load/save keypair
- Call libnorn API functions
- Handle signals for daemon mode
- Proper error handling and exit codes

## Acceptance Criteria

1. ✅ `norn keygen` generates and saves ed25519 keypair
2. ✅ `norn get <key>` retrieves record from DHT
3. ✅ `norn set <key> <value>` stores signed record to DHT
4. ✅ `norn daemon` runs DHT node and responds to queries
5. ✅ `norn version` prints version
6. ✅ All commands have `--help` output
7. ✅ Error handling for missing key file
8. ✅ Error handling for invalid input
9. ✅ Exit codes: 0 success, 1 error, 2 network error
10. ✅ Tests for CLI (integration tests or manual QA)