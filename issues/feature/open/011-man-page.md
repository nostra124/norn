---
id: FEAT-011
type: feature
priority: low
complexity: S
estimate_tokens: 15k-30k
estimate_time: 15-30min
phase: open
status: open
depends_on: [FEAT-010]
milestone: MILESTONE-0.6.0
spawned_from: ~
---
# Man Page

Create a manual page for the norn CLI.

## Description

**As a** norn CLI user
**I want** a man page documenting all commands and options
**So that** I can learn how to use the tool without reading source code

Currently no man page. Need `man/norn.1` with comprehensive documentation.

## Implementation

### File: `man/norn.1`

Sections:
- **NAME** — norn - Mainline DHT client for peer discovery
- **SYNOPSIS** — Usage syntax
- **DESCRIPTION** — What norn does
- **COMMANDS** — keygen, get, set, daemon, version
- **OPTIONS** — Global and command-specific options
- **FILES** — `~/.norn/key.pem`, `~/.norn/config`
- **ENVIRONMENT** — `NORN_KEY`, `NORN_PORT`
- **EXAMPLES** — Common usage patterns
- **EXIT STATUS** — 0, 1, 2
- **SEE ALSO** — Related man pages
- **BUGS** — Issue tracker link
- **AUTHOR** — Author info
- **LICENSE** — MIT License

### Makefile.am Integration

```makefile
man_MANS = man/norn.1
EXTRA_DIST += man/norn.1
```

## Acceptance Criteria

1. ✅ `man/norn.1` created
2. ✅ All commands documented with options and examples
3. ✅ Environment variables documented
4. ✅ Exit codes documented
5. ✅ Man page validates with `man -l man/norn.1`
6. ✅ Install target includes man page
7. ✅ `man norn` displays the manual page after install