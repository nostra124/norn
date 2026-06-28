---
id: FEAT-009
type: feature
priority: low
complexity: S
estimate_tokens: 20k-40k
estimate_time: 20-45min
phase: done
status: done
depends_on: [FEAT-008]
milestone: MILESTONE-0.5.0
spawned_from: ~
---
# Static Analysis Integration

Integrate static analysis tools into the build and CI pipeline.

## Description

**As a** norn library maintainer
**I want** automated static analysis to catch bugs before CI
**So that** code quality issues are detected early

Currently no static analysis. Need cppcheck, clang-tidy, and clang-format integration.

## Implementation

### Configuration Files

`.clang-format` — Code formatting rules:
- IndentWidth: 4
- UseTab: Never
- PointerAlignment: Right
- ColumnLimit: 100

`.clang-tidy` — Static analysis checks:
- bugprone-*, cert-*, clang-analyzer-*
- modernize-*, readability-*

`cppcheck.suppressions` — Suppress known false positives

### Makefile Targets

```makefile
check-cppcheck:
	cppcheck --enable=all --error-exitcode=1 src/libnorn/*.c src/norn.c

check-tidy:
	clang-tidy src/libnorn/*.c -- $(CFLAGS) $(SODIUM_CFLAGS) -I$(top_srcdir)/src/libnorn

check-format:
	clang-format --dry-run --Werror src/libnorn/*.c src/libnorn/*.h src/norn.c

format:
	clang-format -i src/libnorn/*.c src/libnorn/*.h src/norn.c
```

### CI Integration

Already in `.github/workflows/ci.yml` under `static-analysis` job.

## Acceptance Criteria

1. ✅ `.clang-format` created matching project style
2. ✅ `.clang-tidy` created with appropriate checks
3. ✅ `cppcheck.suppressions` created for known issues
4. ✅ `make check-cppcheck` passes
5. ✅ `make check-tidy` passes (after fixes)
6. ✅ `make check-format` passes
7. ✅ `make format` auto-fixes formatting issues
8. ✅ CI runs all static analysis checks
9. ✅ Zero warnings from all tools