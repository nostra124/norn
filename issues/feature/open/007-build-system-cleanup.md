---
id: FEAT-007
type: feature
priority: low
complexity: S
estimate_tokens: 15k-30k
estimate_time: 15-30min
phase: open
status: open
depends_on: []
milestone: MILESTONE-0.4.0
spawned_from: ~
---
# Build System Cleanup

Clean up the autotools build system.

## Description

**As a** norn library maintainer
**I want** a clean build system without duplicates
**So that** builds are reliable and warnings-free

Currently `configure.ac` has duplicate macro calls and `Makefile.am` has coverage script permission issues.

## Implementation

### configure.ac Fixes

Remove duplicate macros:
- `AC_PROG_CC` appears twice (lines 11 and 16)
- `AM_PROG_AR` appears twice (lines 12 and 18)
- `LT_INIT` appears twice (lines 13 and 19)

### Makefile.am Fixes

Coverage target should ensure script is executable:
```makefile
coverage:
	@if [ ! -x "$(top_srcdir)/contrib/coverage.sh" ]; then \
		chmod +x "$(top_srcdir)/contrib/coverage.sh"; \
	fi
	$(top_srcdir)/contrib/coverage.sh
```

### Add -Werror

Make all warnings errors:
```makefile
libnorn_la_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=c99 -I$(top_srcdir)/src/libnorn
```

## Acceptance Criteria

1. ✅ `configure.ac` has no duplicate macros
2. ✅ `make coverage` works without manual chmod
3. ✅ Coverage flag shown in configure output
4. ✅ All tests compile with `-Werror`
5. ✅ `make distcheck` passes