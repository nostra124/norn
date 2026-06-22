---
id: FEAT-008
type: feature
priority: medium
complexity: M
estimate_tokens: 40k-80k
estimate_time: 45-90min
phase: open
status: open
depends_on: [FEAT-001]
milestone: MILESTONE-0.5.0
spawned_from: ~
---
# CI/CD Pipeline (GitHub Actions)

Set up continuous integration for building, testing, and coverage enforcement.

## Description

**As a** norn library maintainer
**I want** automated CI that builds, tests, and enforces 100% coverage on every PR
**So that** code quality is guaranteed before merge

Currently no CI. Need GitHub Actions workflow for Linux/macOS builds, coverage, and static analysis.

## Implementation

### File: `.github/workflows/ci.yml`

Jobs:
1. **build** — Matrix of (ubuntu-latest, macos-latest) × (gcc, clang)
2. **coverage** — Run `make coverage`, enforce 100%, upload to codecov.io
3. **static-analysis** — cppcheck, clang-tidy
4. **format-check** — No tabs, no trailing whitespace, no C++ comments

### Coverage Gate

```yaml
coverage:
  precision: 2
  round: down
  range: "100...100"  # Require 100%
```

### Branch Protection

Configure in GitHub settings:
- Required status checks: build (gcc), build (clang), coverage, static-analysis
- Required PR reviews: 1 approval

## Acceptance Criteria

1. ✅ `.github/workflows/ci.yml` created
2. ✅ CI runs on every push and PR
3. ✅ CI runs on Linux (gcc, clang) and macOS (clang)
4. ✅ Coverage gate enforces 100%
5. ✅ Coverage uploaded to codecov.io
6. ✅ Static analysis runs and passes
7. ✅ Format check runs and passes
8. ✅ Branch protection requires CI to pass
9. ✅ `codecov.yml` configured for 100% requirement