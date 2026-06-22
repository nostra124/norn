ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libnorn.la

libnorn_la_SOURCES = \
	src/libnorn/norn.c \
	src/libnorn/norn_impl.c \
	src/libnorn/norn_transaction.c \
	src/libnorn/kademlia.c \
	src/libnorn/bep44.c \
	src/libnorn/sha1.c \
	src/libnorn/dhtstore.c \
	src/libnorn/recstore.c \
	src/libnorn/bencode.c \
	src/libnorn/crypto.c \
	src/libnorn/net.c \
	src/libnorn/log.c \
	src/libnorn/channel.c \
	src/libnorn/replaycache.c

libnorn_la_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -O2 -I$(top_srcdir)/src/libnorn

include_HEADERS = src/libnorn/norn.h

bin_PROGRAMS = norn

norn_SOURCES = src/norn.c
norn_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -O2 -I$(top_srcdir)/src/libnorn
norn_LDADD = libnorn.la $(SODIUM_LIBS)

check_PROGRAMS = test_norn test_bep44 test_dhtstore test_bencode test_crypto test_log test_kademlia test_recstore test_norn_async test_mainline test_channel test_channel_edges test_replaycache

test_norn_SOURCES = tests/test_norn.c
test_norn_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_norn_LDADD = libnorn.la $(SODIUM_LIBS)

test_bep44_SOURCES = tests/test_bep44.c
test_bep44_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_bep44_LDADD = libnorn.la $(SODIUM_LIBS)

test_dhtstore_SOURCES = tests/test_dhtstore.c
test_dhtstore_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_dhtstore_LDADD = libnorn.la $(SODIUM_LIBS)

test_bencode_SOURCES = tests/test_bencode.c
test_bencode_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_bencode_LDADD = libnorn.la $(SODIUM_LIBS)

test_crypto_SOURCES = tests/test_crypto.c
test_crypto_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_crypto_LDADD = libnorn.la $(SODIUM_LIBS)

test_log_SOURCES = tests/test_log.c
test_log_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_log_LDADD = libnorn.la $(SODIUM_LIBS)

test_kademlia_SOURCES = tests/test_kademlia.c
test_kademlia_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_kademlia_LDADD = libnorn.la $(SODIUM_LIBS)

test_recstore_SOURCES = tests/test_recstore.c
test_recstore_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_recstore_LDADD = libnorn.la $(SODIUM_LIBS)

test_norn_async_SOURCES = tests/test_norn_async.c
test_norn_async_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_norn_async_LDADD = libnorn.la $(SODIUM_LIBS)

test_mainline_SOURCES = tests/test_mainline.c
test_mainline_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_mainline_LDADD = libnorn.la $(SODIUM_LIBS)

test_channel_SOURCES = tests/test_channel.c
test_channel_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_channel_LDADD = libnorn.la $(SODIUM_LIBS)

test_channel_edges_SOURCES = tests/test_channel_edges.c
test_channel_edges_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_channel_edges_LDADD = libnorn.la $(SODIUM_LIBS)

test_replaycache_SOURCES = tests/test_replaycache.c
test_replaycache_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_replaycache_LDADD = libnorn.la $(SODIUM_LIBS)

TESTS = $(check_PROGRAMS)

# Coverage support
if ENABLE_COVERAGE
COV_FLAGS = -fprofile-arcs -ftest-coverage
libnorn_la_CFLAGS += $(COV_FLAGS)
norn_CFLAGS += $(COV_FLAGS)
endif

coverage:
	@if [ ! -x "$(top_srcdir)/contrib/coverage.sh" ]; then \
		chmod +x "$(top_srcdir)/contrib/coverage.sh"; \
	fi
	$(top_srcdir)/contrib/coverage.sh

# System Integration Tests (SIT)
check-sit:
	@if command -v bats >/dev/null 2>&1; then \
		bats tests/sit/*.bats; \
	else \
		echo "ERROR: bats not installed. Install bats or run: ./tests/sit/run.sh"; \
		exit 1; \
	fi

# Performance Integration Tests (PIT)
check-pit:
	@if command -v bats >/dev/null 2>&1; then \
		bats tests/pit/*.bats; \
	else \
		echo "ERROR: bats not installed. Install bats or run: ./tests/pit/run.sh"; \
		exit 1; \
	fi

# All tests (unit + SIT + PIT)
check-all: check check-sit check-pit

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = norn.pc

EXTRA_DIST = README.md LICENSE docs AGENTS.md tests/sit tests/pit