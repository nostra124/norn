ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libnorn.la

libnorn_la_SOURCES = \
	src/libnorn/norn.c \
	src/libnorn/norn_impl.c \
	src/libnorn/kademlia.c \
	src/libnorn/bep44.c \
	src/libnorn/sha1.c \
	src/libnorn/dhtstore.c \
	src/libnorn/recstore.c \
	src/libnorn/bencode.c \
	src/libnorn/crypto.c \
	src/libnorn/net.c \
	src/libnorn/log.c

libnorn_la_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -O2 -I$(top_srcdir)/src/libnorn

include_HEADERS = src/libnorn/norn.h

bin_PROGRAMS = norn

norn_SOURCES = src/norn.c
norn_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -O2 -I$(top_srcdir)/src/libnorn
norn_LDADD = libnorn.la $(SODIUM_LIBS)

check_PROGRAMS = test_norn test_bep44 test_dhtstore

test_norn_SOURCES = tests/test_norn.c
test_norn_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_norn_LDADD = libnorn.la $(SODIUM_LIBS)

test_bep44_SOURCES = tests/test_bep44.c
test_bep44_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_bep44_LDADD = libnorn.la $(SODIUM_LIBS)

test_dhtstore_SOURCES = tests/test_dhtstore.c
test_dhtstore_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -std=c99 -I$(top_srcdir)/src/libnorn
test_dhtstore_LDADD = libnorn.la $(SODIUM_LIBS)

TESTS = $(check_PROGRAMS)

# Coverage support
if ENABLE_COVERAGE
COV_FLAGS = -fprofile-arcs -ftest-coverage
libnorn_la_CFLAGS += $(COV_FLAGS)
norn_CFLAGS += $(COV_FLAGS)
endif

coverage:
	$(top_srcdir)/contrib/coverage.sh

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = norn.pc

EXTRA_DIST = README.md LICENSE docs AGENTS.md