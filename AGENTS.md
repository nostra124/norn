# norn — Development Methodology

## TDD (Test-Driven Development)
Every feature follows the file-first issue → failing test → implement → commit pattern:
1. Write the issue describing the feature
2. Write a failing test
3. Implement just enough to pass
4. Commit with issue reference

## Coverage
All new code must have 100% line and branch coverage. The `make coverage` target enforces this.

## Code Style
- C99 with libsodium for crypto
- No heap allocations in hot paths (use arenas/pools where needed)
- All public APIs return int (0 success, -1 error) or pointer (NULL on error)
- NULL-safe: all public functions handle NULL inputs gracefully
- Header files document invariants and ownership

## File Organization
```
src/libnorn/     — library implementation
src/norn.c       — CLI (thin wrapper over library)
tests/           — unit tests (one test_*.c per source file)
docs/            — architecture docs, BEP references
```

## Supported Platforms
Same as bifrost:
- Linux: alpine, debian, ubuntu, fedora, rhel, centos, arch, opensuse
- macOS: homebrew, macports
- FreeBSD

## Dependencies
- libsodium >= 1.0.0
- autoconf, automake, libtool (build-only)
- gcc or clang
- make

## Building
```
./autogen.sh
./configure
make
make check
sudo make install
```

## Testing
```
make check       # Run unit tests
make coverage    # Generate coverage report (enforces 100%)
```