---
id: FEAT-005
type: feature
priority: medium
complexity: S
estimate_tokens: 20k-40k
estimate_time: 20-45min
phase: open
status: open
depends_on: []
milestone: MILESTONE-0.3.0
spawned_from: ~
---
# CONTRIBUTING Guide

Create a comprehensive guide for new contributors.

## Description

**As a** new contributor
**I want** a CONTRIBUTING guide explaining setup, workflow, and conventions
**So that** I can contribute without asking questions

Currently no CONTRIBUTING.md. Need guide covering prerequisites, build, test, TDD workflow, code style, and PR process.

## Implementation

### File: `CONTRIBUTING.md`

Sections:
1. **Development Setup** — Prerequisites, installation (Linux/macOS/FreeBSD), building
2. **Development Workflow** — Write issue, write failing test, implement, commit
3. **TDD** — Test-Driven Development enforcement
4. **Code Style** — C99, naming conventions, memory rules, comments
5. **Testing Guidelines** — What to test, test structure
6. **Pull Request Process** — Issue, branch, PR, review
7. **Code Review Checklist** — Issue exists, tests exist, coverage 100%, etc.
8. **Getting Help** — Where to ask questions

## Acceptance Criteria

1. ✅ `CONTRIBUTING.md` created at repo root
2. ✅ Development setup documented for Linux, macOS, FreeBSD
3. ✅ TDD workflow documented with examples
4. ✅ Code style documented (C99, naming, memory)
5. ✅ Coverage requirements documented (100% line + branch)
6. ✅ PR process documented
7. ✅ New contributor can build and test without asking questions
8. ✅ Code review checklist provided