# v0.6.0 — User Experience

Functional CLI for end users.

## Status (2026-06-23)

**DONE** — CLI and man page complete

## Tickets

| ID | Title | Status |
|----|-------|--------|
| FEAT-010 | CLI Implementation | done |
| FEAT-011 | Man Page | done |

## Implementation

### FEAT-010: CLI Implementation

Complete rewrite of `src/norn.c`:
- **keygen**: Generate ed25519 keypair, save to `~/.norn/key.pem`, print pubkey
- **get**: Retrieve record from DHT (stub - requires running DHT)
- **set**: Store signed record to DHT (stub - requires running DHT)
- **daemon**: Run DHT daemon (stub - requires event loop)
- **version**: Print version

Global options:
- `--key <path>`: Key file path (default: `~/.norn/key.pem`)
- `--port <port>`: DHT port (default: 6881)
- `--timeout <ms>`: Query timeout (default: 5000)
- `--log-level <level>`: Log level (debug, info, warn, error)
- `--read-only`: Enable BEP-43 read-only mode
- `--help`: Show help

Environment variables:
- `NORN_KEY`: Default key file path
- `NORN_PORT`: Default DHT port

Exit codes:
- 0: Success
- 1: Error
- 2: Network error (reserved for future use)

### FEAT-011: Man Page

Created `man/norn.1` with:
- NAME, SYNOPSIS, DESCRIPTION
- COMMANDS (keygen, get, set, daemon, version)
- OPTIONS (all global and command-specific)
- ENVIRONMENT (NORN_KEY, NORN_PORT)
- FILES (~/.norn/key.pem)
- EXIT STATUS (0, 1, 2)
- EXAMPLES (common usage patterns)
- SEE ALSO (bifrost, dht, BEP-5, BEP-43, BEP-44)
- BUGS, AUTHOR, LICENSE

Added to Makefile.am:
- `man_MANS = man/norn.1`
- `EXTRA_DIST += man/norn.1`

## Tests

- All 26 unit tests pass
- CLI compiles with `-Werror`
- Command-specific `--help` works correctly
- Global `--help` works correctly
- Environment variables recognized