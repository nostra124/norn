# Project Profile — norn

## What this project IS

norn is a **C library** implementing a **Mainline DHT client** for P2P peer discovery and bootstrap. It provides:

- `libnorn` — In-memory DHT client library (no config/file I/O)
- `norn` — CLI tool for DHT operations (keygen, get, set, daemon)

## Language

- **C99** with libsodium for crypto
- **Autotools** build system (autoconf, automake, libtool)
- **Make** for building and testing

## Dependencies

- libsodium >= 1.0.0
- autoconf, automake, libtool (build-only)
- gcc or clang
- make

## Build commands

```bash
autoreconf -fi
./configure
make
make check
make coverage
```

## Testing approach

- **Unit tests**: `tests/test_*.c` (one per source file)
- **Test runner**: `make check`
- **Coverage**: `make coverage` (enforces 100% line + branch)
- **Coverage-tracked files**: `tests/coverage-tracked.txt`

## Code style

- C99 with libsodium for crypto
- No heap allocations in hot paths (use arenas/pools where needed)
- All public APIs return int (0 success, -1 error) or pointer (NULL on error)
- NULL-safe: all public functions handle NULL inputs gracefully
- Header files document invariants and ownership
- No C++ comments (`//`), use `/* */`
- No tabs, use spaces
- Max line length: 100 characters

## Supported platforms

Same as bifrost:
- Linux: alpine, debian, ubuntu, fedora, rhel, centos, arch, opensuse
- macOS: homebrew, macports
- FreeBSD

## Project-specific conventions

### Issue IDs

Issue IDs follow the pattern `FEAT-NNN` (features) or `BUG-NNN` (bugs), **without** a project prefix like `NORN-`. This aligns with the methodology used across related projects.

### Milestones

Milestones are version-based: `MILESTONE-X.Y.Z.md` files under `.repo/project/issues/`. Each milestone lists tickets in a table.

### Ticket phases

- Features: `open/`, `design/`, `build/`, `test/`, `done/`
- Bugs: `open/`, `build/`, `test/`, `done/` (skip `design/`)

### Storage locations

| Location | What goes here |
|---|---|
| `.repo/norn/` | Agent session notes, project-specific instructions |
| `docs/` | Architecture, BEP references, porting guide |
| `issues/` | All tracked work: FEAT, BUG tickets |
| `.repo/project/issues/` | Milestone files |

## Key files

- `README.md` — Project overview
- `AGENTS.md` — Agent bootstrap (this file)
- `.repo/norn/notes.md` — Previous session notes
- `Makefile.am` — Build configuration
- `tests/coverage-tracked.txt` — Files requiring 100% coverage