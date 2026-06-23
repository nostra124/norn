---
id: FEAT-006
type: feature
priority: medium
complexity: M
estimate_tokens: 50k-100k
estimate_time: 60-120min
phase: done
status: done
depends_on: []
milestone: MILESTONE-0.4.0
spawned_from: ~
---
# Split norn.c into Focused Modules

Refactor the 1100+ line `norn.c` into smaller, focused modules for maintainability.

## Description

**As a** norn library maintainer
**I want** source files under 500 lines each with single responsibility
**So that** the codebase is maintainable and easier to understand

Currently `src/libnorn/norn.c` is ~1100 lines mixing public API, routing table, transactions, and DHT server logic. Need to split into focused modules.

## Implementation

### Current State

`src/libnorn/norn.c` contains:
- Mainline DHT client implementation
- Routing table management
- Transaction handling
- Kademlia lookup algorithms
- DHT server logic
- Peer cache

### Proposed Split

1. **client.c** — Public API wrapper (~200 lines)
   - `norn_new`, `norn_free`, `norn_bootstrap`, `norn_put/get_*`, etc.
   
2. **mainline.c** — Already exists (~800 lines, keep as-is for now)

3. Future considerations (not in this ticket):
   - `transaction.c` — Transaction tracking (~150 lines)
   - `routing.c` — Routing table operations (~200 lines)

### Minimal Refactoring

The primary goal is to create `client.c` as the public API implementation, keeping `mainline.c` unchanged. This provides a clean separation between public API and protocol implementation.

### File Structure After

```
src/libnorn/
├── norn.h              # Public API header
├── client.c            # Public API implementation (~200 lines)
├── mainline.h          # DHT protocol header
├── mainline.c          # DHT protocol (~800 lines)
├── bep44.h / bep44.c   # BEP-44 codec (~190 lines)
├── dhtstore.h / dhtstore.c  # DHT store (~210 lines)
├── ... (other modules unchanged)
```

## Acceptance Criteria

1. ✅ `client.c` created with public API implementation
2. ✅ `norn.c` removed (renamed to `client.c`)
3. ✅ `norn.h` unchanged (public API preserved)
4. ✅ All tests pass after refactoring
5. ✅ Coverage remains 100% after refactoring
6. ✅ No source file exceeds 500 lines (except mainline.c at 800)
7. ✅ Each module has single responsibility
8. ✅ Public API unchanged (norn.h)