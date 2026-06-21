---
id: NORN-001
type: feature
priority: high
status: in_progress
depends_on: []
---
# Core library implementation

Implement the core norn library API as defined in `norn.h`:
- Client lifecycle (norn_new, norn_free)
- DHT operations (bootstrap, put/get mutable/immutable, announce, discover)
- Event loop integration (tick, get_fd)

## Test Plan (TDD)
1. Write test_norn_client.c - client lifecycle tests
2. Write test_norn_put_get.c - put/get operations (mocked network)
3. Implement API to pass tests
4. 100% line and branch coverage

## Files
- src/libnorn/norn.c — main implementation
- src/libnorn/norn_impl.c — API wrapper (current stub)
- tests/test_norn.c — client lifecycle tests
- tests/test_bep44.c — BEP-44 encoding tests
- tests/test_dhtstore.c — DHT storage tests