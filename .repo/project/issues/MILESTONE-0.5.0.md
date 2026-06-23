# v0.5.0 — Infrastructure

Automation and continuous integration.

## Status (2026-06-23)

**DONE** — CI/CD pipeline and static analysis integration complete

## Tickets

| ID | Title | Status |
|----|-------|--------|
| FEAT-008 | CI/CD Pipeline (GitHub Actions) | done |
| FEAT-009 | Static Analysis Integration | done |

## Implementation

### FEAT-008: CI/CD Pipeline

Created `.github/workflows/ci.yml` with jobs:
- **build**: Matrix of (ubuntu-latest, macos-latest) × (gcc, clang)
- **coverage**: Runs `make coverage`, uploads to codecov.io, enforces 100%
- **static-analysis**: Runs cppcheck, clang-tidy, clang-format
- **distcheck**: Runs `make distcheck` to verify distribution

Created `codecov.yml`:
- 100% coverage requirement for project and patch
- Ignores tests/, src/norn.c, src/libnorn/norn_impl.c, src/libnorn/net.c

### FEAT-009: Static Analysis Integration

Created configuration files:
- `.clang-format`: LLVM style, 4-space indent, no tabs
- `.clang-tidy`: bugprone-*, cert-*, clang-analyzer-*, modernize-*, performance-*
- `cppcheck.supp`: Suppressions for known issues

Created Makefile targets:
- `make check-cppcheck`: Runs cppcheck with error exit code
- `make check-tidy`: Runs clang-tidy (optional, requires clang-tidy)
- `make check-format`: Checks code formatting (optional, requires clang-format)
- `make format`: Auto-formats code with clang-format

## Results

- ✅ All 26 tests pass
- ✅ cppcheck passes with 0 errors
- ✅ CI workflow ready for GitHub Actions
- ✅ Coverage gate configured for 100%