---
id: FEAT-004
type: feature
priority: medium
complexity: M
estimate_tokens: 40k-80k
estimate_time: 45-90min
phase: open
status: open
depends_on: []
milestone: MILESTONE-0.3.0
spawned_from: ~
---
# API Reference Documentation

Document every public function in header files with purpose, parameters, return values, and ownership.

## Description

**As a** norn library user
**I want** complete API documentation in header files
**So that** I can use the library without reading implementation files

Currently headers have minimal comments. Need Doxygen-style documentation for every public function with @brief, @param, @return, @note, and example code.

## Implementation

### Files to Document

- `src/libnorn/norn.h` — Public API (14 functions)
- `src/libnorn/bep44.h` — BEP-44 codec (10 functions)
- `src/libnorn/dhtstore.h` — DHT store (8 functions)
- `src/libnorn/log.h` — Logging (6 functions)

### Documentation Format

```c
/**
 * @brief Brief one-line description
 *
 * Detailed description of what the function does, when to use it,
 * and any important behavioral notes.
 *
 * @param param_name Description of this parameter
 * @param another_param Description of another parameter
 * @return Description of return value (0 success, -1 error, etc.)
 *
 * @note Thread safety: Is this function thread-safe?
 * @note Ownership: Who owns allocated memory?
 *
 * Example:
 *   unsigned char id[20];
 *   if (norn_get_id(client, id) == 0) {
 *       printf("Node ID: %02x%02x...\n", id[0], id[1]);
 *   }
 */
```

### Documentation Checklist

For each function:
- [ ] Brief description (one sentence)
- [ ] Detailed description (paragraph)
- [ ] All parameters documented
- [ ] Return value documented
- [ ] NULL-safety noted
- [ ] Thread safety noted
- [ ] Ownership noted
- [ ] Example code provided
- [ ] Error conditions documented

## Acceptance Criteria

1. ✅ Every public function in `norn.h` documented
2. ✅ Every public function in `bep44.h` documented
3. ✅ Every public function in `dhtstore.h` documented
4. ✅ Every public function in `log.h` documented
5. ✅ All NULL-safety guarantees documented
6. ✅ All ownership guarantees documented
7. ✅ All thread-safety guarantees documented
8. ✅ Example code for each function compiles