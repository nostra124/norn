---
id: FEAT-002
type: feature
priority: high
complexity: M
estimate_tokens: 40k-80k
estimate_time: 45-90min
phase: open
status: done
depends_on: []
milestone: MILESTONE-0.2.0
spawned_from: ~
---
# Logging Module with Full Coverage

Implement a structured logging module with 100% test coverage supporting debug, info, warn, and error levels.

## Description

**As a** norn library user
**I want** structured logging with configurable levels (debug, info, warn, error)
**So that** I can diagnose issues and monitor library behavior in production

Currently norn has no unified logging. The mainline module uses `fprintf` directly. We need a proper logging module with level filtering, file/line attribution, and 100% test coverage.

## Implementation

### API Design

```c
/* log.h */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} log_level_t;

int log_init(log_level_t min_level, void (*sink)(const char *line));
void log_set_level(log_level_t level);
void log_write(log_level_t level, const char *file, int line, const char *fmt, ...);
void log_shutdown(void);

#define LOG_DEBUG(...) log_write(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
```

### Output Format

```
[2025-06-21T10:30:45Z] INFO mainline.c:142 - bootstrap: contacted router.bittorrent.com:6881
```

### File: `src/libnorn/log.c`

Implementation with:
- Level filtering (don't emit below min_level)
- Custom sink (for daemon/syslog integration) or stderr fallback
- No heap allocations in hot path (stack buffers)
- Thread-safe write (single writev call)

### Test Plan (`tests/test_log.c`)

1. **Level tests** — all 4 levels emit correctly
2. **Filtering tests** — below min_level filtered
3. **Format tests** — timestamp, level, file, line present
4. **Sink tests** — custom sink receives lines
5. **Fallback tests** — NULL sink writes to stderr
6. **Long message tests** — truncation handled safely
7. **NULL input tests** — NULL format string doesn't crash

## Acceptance Criteria

1. ✅ `src/libnorn/log.h` defines API with 4 log levels
2. ✅ `src/libnorn/log.c` implements logging with level filtering
3. ✅ `tests/test_log.c` tests all paths with 100% coverage
4. ✅ `make coverage` shows 100% line and branch coverage for `log.c`
5. ✅ All 4 log levels tested
6. ✅ Level filtering tested
7. ✅ NULL sink (stderr) path tested
8. ✅ Format string edge cases tested
9. ✅ No heap allocations in log_write (stack buffers only)
10. ✅ Integration with mainline module (replace ML_LOG)