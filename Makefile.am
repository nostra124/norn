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
	src/libnorn/replaycache.c \
	src/libnorn/stream.c \
	src/libnorn/streammux.c \
	src/libnorn/norn_forward.c \
	src/libnorn/norn_raft.c \
	src/libnorn/norn_kvstore.c \
	src/libnorn/norn_cluster.c \
	src/libnorn/norn_overlay.c \
	src/libnorn/transport.c \
	src/libnorn/transport_udp.c \
	src/libnorn/transport_tcp.c \
	src/libnorn/idexch.c \
	src/libnorn/attr.c \
	src/libnorn/norn_suite_sodium.c \
	src/libnorn/norn_kad.c \
	src/libnorn/norn_idexch.c \
	src/libnorn/norn_session.c \
	src/libnorn/norn_session_udp.c \
	src/libnorn/norn_endpoint_cache.c \
	src/libnorn/norn_nat.c \
	src/libnorn/norn_rendezvous.c \
	src/libnorn/norn_relay.c \
	src/libnorn/norn_bonjour.c \
	src/libnorn/norn_upnp.c

libnorn_la_CFLAGS = $(SODIUM_CFLAGS) $(AVAHI_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
# Record libsodium as a dependency of the shared library itself, so dynamic
# consumers (e.g. the Rust binding) don't have to re-link it explicitly and it
# survives the linker's --as-needed pruning.
libnorn_la_LIBADD = $(SODIUM_LIBS) $(AVAHI_LIBS)

# nornd — reference node daemon + CLI, built as an application ON libnorn
# (not part of the library). Convenience lib so the daemon and its tests share
# the same object code.
noinst_LTLIBRARIES = libnornd.la
libnornd_la_SOURCES = src/nornd/ipc.c src/nornd/identity.c src/nornd/dispatch.c src/nornd/client.c src/nornd/cli_cluster.c src/nornd/cli_keys.c src/nornd/cli_peer.c src/nornd/keydir.c src/nornd/served_proto.c src/nornd/store.c src/nornd/served.c src/nornd/served_conn.c src/nornd/agent.c src/nornd/peers.c src/nornd/transport.c
libnornd_la_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd

# Internal headers: not installed, but must ship in the tarball (they're
# #included by the sources) so a VPATH/dist build can find them.
noinst_HEADERS = src/nornd/ipc.h src/nornd/identity.h src/nornd/dispatch.h \
                 src/nornd/client.h src/nornd/cli_cluster.h src/nornd/cli_keys.h \
                 src/nornd/cli_peer.h \
                 src/nornd/keydir.h src/nornd/served_proto.h src/nornd/store.h \
                 src/nornd/served.h src/nornd/served_conn.h src/nornd/agent.h \
                 src/nornd/peers.h src/nornd/transport.h \
                 src/libnorn/norn_internal.h src/libnorn/norn_session_internal.h \
                 src/libnorn/norn_endpoint_cache.h src/libnorn/norn_upnp.h \
                 src/libnorn/norn_bonjour.h

include_HEADERS = src/libnorn/norn.h \
                   src/libnorn/mainline.h \
                   src/libnorn/norn_transaction.h \
                   src/libnorn/norn_suite.h \
                   src/libnorn/norn_kad.h \
                   src/libnorn/norn_idexch.h \
                   src/libnorn/norn_session.h \
                   src/libnorn/norn_nat.h \
                   src/libnorn/norn_rendezvous.h \
                   src/libnorn/norn_relay.h \
                   src/libnorn/crypto.h \
                   src/libnorn/bep44.h \
                   src/libnorn/bencode.h \
                   src/libnorn/dhtstore.h \
                   src/libnorn/recstore.h \
                   src/libnorn/kademlia.h \
                   src/libnorn/log.h \
                   src/libnorn/channel.h \
                   src/libnorn/replaycache.h \
                   src/libnorn/stream.h \
                   src/libnorn/streammux.h \
                   src/libnorn/norn_forward.h \
                   src/libnorn/norn_raft.h \
                   src/libnorn/norn_kvstore.h \
                   src/libnorn/norn_cluster.h \
                   src/libnorn/norn_overlay.h \
                   src/libnorn/transport.h \
                   src/libnorn/transport_udp.h \
                   src/libnorn/transport_tcp.h \
                   src/libnorn/idexch.h \
                   src/libnorn/attr.h \
                   src/libnorn/sha1.h \
                   src/libnorn/net.h

bin_PROGRAMS = norn norn-forward nornd

norn_SOURCES = src/norn.c
norn_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
norn_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

norn_forward_SOURCES = src/norn_forward_main.c
norn_forward_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
norn_forward_LDADD = libnorn.la $(SODIUM_LIBS)

nornd_SOURCES = src/nornd/main.c
nornd_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
nornd_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

# Service packaging (FEAT-032). Installed under $(prefix) so a sandboxed
# `make install` never collides with system paths; distro packaging relocates
# them to /usr/lib/systemd and /Library/Launch* as appropriate.
systemdsystemdir = $(prefix)/lib/systemd/system
dist_systemdsystem_DATA = contrib/systemd/nornd.socket contrib/systemd/nornd.service
systemduserdir = $(prefix)/lib/systemd/user
dist_systemduser_DATA = contrib/systemd/user/nornd.socket contrib/systemd/user/nornd.service
launchddir = $(prefix)/lib/launchd
dist_launchd_DATA = contrib/launchd/io.norn.nornd.plist contrib/launchd/io.norn.nornd.user.plist

check_PROGRAMS = test_norn test_bep44 test_dhtstore test_bencode test_crypto test_log test_kademlia test_recstore test_norn_async test_mainline test_channel test_channel_edges test_replaycache test_stream test_stream_edges test_stream_recovery test_stream_cc test_stream_random test_streammux test_streammux_random test_forward test_raft test_kvstore test_cluster test_overlay test_overlay_net test_transport test_transport_udp test_transport_tcp test_idexch test_attr test_net test_suite test_norn_kad test_norn_idexch test_norn_session test_session_loopback test_session_accept test_rendezvous test_relay test_nornd_ipc test_nornd_identity test_nornd_dispatch test_nornd_client test_nornd_keydir test_nornd_served_proto test_nornd_peers test_nornd_store test_nornd_served test_nornd_agent test_nornd_served_dial

test_norn_SOURCES = tests/test_norn.c
test_norn_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_norn_LDADD = libnorn.la $(SODIUM_LIBS)

test_bep44_SOURCES = tests/test_bep44.c
test_bep44_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_bep44_LDADD = libnorn.la $(SODIUM_LIBS)

test_dhtstore_SOURCES = tests/test_dhtstore.c
test_dhtstore_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_dhtstore_LDADD = libnorn.la $(SODIUM_LIBS)

test_bencode_SOURCES = tests/test_bencode.c
test_bencode_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_bencode_LDADD = libnorn.la $(SODIUM_LIBS)

test_crypto_SOURCES = tests/test_crypto.c
test_crypto_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_crypto_LDADD = libnorn.la $(SODIUM_LIBS)

test_log_SOURCES = tests/test_log.c
test_log_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_log_LDADD = libnorn.la $(SODIUM_LIBS)

test_kademlia_SOURCES = tests/test_kademlia.c
test_kademlia_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_kademlia_LDADD = libnorn.la $(SODIUM_LIBS)

test_recstore_SOURCES = tests/test_recstore.c
test_recstore_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_recstore_LDADD = libnorn.la $(SODIUM_LIBS)

test_norn_async_SOURCES = tests/test_norn_async.c
test_norn_async_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_norn_async_LDADD = libnorn.la $(SODIUM_LIBS)

test_mainline_SOURCES = tests/test_mainline.c
test_mainline_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_mainline_LDADD = libnorn.la $(SODIUM_LIBS)

test_channel_SOURCES = tests/test_channel.c
test_channel_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_channel_LDADD = libnorn.la $(SODIUM_LIBS)

test_channel_edges_SOURCES = tests/test_channel_edges.c
test_channel_edges_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_channel_edges_LDADD = libnorn.la $(SODIUM_LIBS)

test_replaycache_SOURCES = tests/test_replaycache.c
test_replaycache_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_replaycache_LDADD = libnorn.la $(SODIUM_LIBS)

test_stream_SOURCES = tests/test_stream.c
test_stream_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_stream_LDADD = libnorn.la $(SODIUM_LIBS)

test_stream_edges_SOURCES = tests/test_stream_edges.c
test_stream_edges_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_stream_edges_LDADD = libnorn.la $(SODIUM_LIBS)

test_stream_recovery_SOURCES = tests/test_stream_recovery.c
test_stream_recovery_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_stream_recovery_LDADD = libnorn.la $(SODIUM_LIBS)

test_stream_cc_SOURCES = tests/test_stream_cc.c
test_stream_cc_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_stream_cc_LDADD = libnorn.la $(SODIUM_LIBS)

test_stream_random_SOURCES = tests/test_stream_random.c
test_stream_random_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_stream_random_LDADD = libnorn.la $(SODIUM_LIBS)

test_streammux_SOURCES = tests/test_streammux.c
test_streammux_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_streammux_LDADD = libnorn.la $(SODIUM_LIBS)

test_streammux_random_SOURCES = tests/test_streammux_random.c
test_streammux_random_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_streammux_random_LDADD = libnorn.la $(SODIUM_LIBS)

test_forward_SOURCES = tests/test_forward.c
test_forward_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_forward_LDADD = libnorn.la $(SODIUM_LIBS)

test_raft_SOURCES = tests/test_raft.c
test_raft_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_raft_LDADD = libnorn.la $(SODIUM_LIBS)

test_kvstore_SOURCES = tests/test_kvstore.c
test_kvstore_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_kvstore_LDADD = libnorn.la $(SODIUM_LIBS)

test_cluster_SOURCES = tests/test_cluster.c
test_cluster_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_cluster_LDADD = libnorn.la $(SODIUM_LIBS)

test_overlay_SOURCES = tests/test_overlay.c
test_overlay_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_overlay_LDADD = libnorn.la $(SODIUM_LIBS)

test_overlay_net_SOURCES = tests/test_overlay_net.c
test_overlay_net_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_overlay_net_LDADD = libnorn.la $(SODIUM_LIBS)

test_transport_SOURCES = tests/test_transport.c
test_transport_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_transport_LDADD = libnorn.la $(SODIUM_LIBS)

test_transport_udp_SOURCES = tests/test_transport_udp.c
test_transport_udp_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_transport_udp_LDADD = libnorn.la $(SODIUM_LIBS)

test_transport_tcp_SOURCES = tests/test_transport_tcp.c
test_transport_tcp_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_transport_tcp_LDADD = libnorn.la $(SODIUM_LIBS)

test_idexch_SOURCES = tests/test_idexch.c
test_idexch_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_idexch_LDADD = libnorn.la $(SODIUM_LIBS)

test_attr_SOURCES = tests/test_attr.c
test_attr_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_attr_LDADD = libnorn.la $(SODIUM_LIBS)

test_net_SOURCES = tests/test_net.c
test_net_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_net_LDADD = libnorn.la $(SODIUM_LIBS)

test_suite_SOURCES = tests/test_suite.c
test_suite_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_suite_LDADD = libnorn.la $(SODIUM_LIBS)

test_norn_kad_SOURCES = tests/test_norn_kad.c
test_norn_kad_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_norn_kad_LDADD = libnorn.la $(SODIUM_LIBS)

test_norn_idexch_SOURCES = tests/test_norn_idexch.c
test_norn_idexch_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_norn_idexch_LDADD = libnorn.la $(SODIUM_LIBS)

test_norn_session_SOURCES = tests/test_norn_session.c
test_norn_session_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_norn_session_LDADD = libnorn.la $(SODIUM_LIBS)

test_session_loopback_SOURCES = tests/test_session_loopback.c
test_session_loopback_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_session_loopback_LDADD = libnorn.la $(SODIUM_LIBS)

test_session_accept_SOURCES = tests/test_session_accept.c
test_session_accept_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_session_accept_LDADD = libnorn.la $(SODIUM_LIBS)

test_rendezvous_SOURCES = tests/test_rendezvous.c
test_rendezvous_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_rendezvous_LDADD = libnorn.la $(SODIUM_LIBS)

test_relay_SOURCES = tests/test_relay.c
test_relay_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn
test_relay_LDADD = libnorn.la $(SODIUM_LIBS)

test_nornd_ipc_SOURCES = tests/test_nornd_ipc.c
test_nornd_ipc_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_ipc_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

test_nornd_identity_SOURCES = tests/test_nornd_identity.c
test_nornd_identity_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_identity_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

test_nornd_dispatch_SOURCES = tests/test_nornd_dispatch.c
test_nornd_dispatch_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_dispatch_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

test_nornd_client_SOURCES = tests/test_nornd_client.c
test_nornd_client_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_client_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

test_nornd_keydir_SOURCES = tests/test_nornd_keydir.c
test_nornd_keydir_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_keydir_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

test_nornd_served_proto_SOURCES = tests/test_nornd_served_proto.c
test_nornd_served_proto_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_served_proto_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

test_nornd_peers_SOURCES = tests/test_nornd_peers.c
test_nornd_peers_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_peers_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

test_nornd_store_SOURCES = tests/test_nornd_store.c
test_nornd_store_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_store_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

test_nornd_served_SOURCES = tests/test_nornd_served.c
test_nornd_served_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_served_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

test_nornd_agent_SOURCES = tests/test_nornd_agent.c
test_nornd_agent_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_agent_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

test_nornd_served_dial_SOURCES = tests/test_nornd_served_dial.c
test_nornd_served_dial_CFLAGS = $(SODIUM_CFLAGS) -Wall -Wextra -Werror -std=gnu99 -I$(top_srcdir)/src/libnorn -I$(top_srcdir)/src/nornd
test_nornd_served_dial_LDADD = libnornd.la libnorn.la $(SODIUM_LIBS)

TESTS = $(check_PROGRAMS)

# Coverage support
if ENABLE_COVERAGE
COV_FLAGS = -fprofile-arcs -ftest-coverage
libnorn_la_CFLAGS += $(COV_FLAGS)
libnornd_la_CFLAGS += $(COV_FLAGS)
norn_CFLAGS += $(COV_FLAGS)
endif

coverage:
	@if [ ! -x "$(top_srcdir)/contrib/coverage.sh" ]; then \
		chmod +x "$(top_srcdir)/contrib/coverage.sh"; \
	fi
	$(top_srcdir)/contrib/coverage.sh

# API documentation (Doxygen). Generates HTML + man3 from the header comments
# into docs/api/. The headers already use Doxygen markup (@file/@brief/@param).
# Named `apidocs` (not `docs`) so it never collides with the docs/ directory,
# which is shipped via EXTRA_DIST and would otherwise be treated as a target.
.PHONY: apidocs clean-apidocs coverage check-sit check-pit
apidocs:
	@command -v doxygen >/dev/null 2>&1 || { \
		echo "ERROR: doxygen not found. Install it (e.g. apt-get install doxygen graphviz)."; \
		exit 1; \
	}
	doxygen Doxyfile

clean-apidocs:
	rm -rf docs/api

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

# Man pages are generated from .in templates so the version tracks the package.
man_MANS = man/norn.1 man/nornd.8 man/libnorn.3
CLEANFILES = $(man_MANS)

EXTRA_DIST = README.md LICENSE docs AGENTS.md tests/sit tests/pit \
             .clang-format .clang-tidy cppcheck.supp codecov.yml \
             contrib/coverage.sh tests/coverage-tracked.txt \
             man/norn.1.in man/nornd.8.in man/libnorn.3.in Doxyfile.in VERSION \
             src/libnorn/mainline.h \
             src/libnorn/norn_transaction.h \
             src/libnorn/norn_suite.h \
             src/libnorn/crypto.h \
             src/libnorn/bep44.h \
             src/libnorn/bencode.h \
             src/libnorn/dhtstore.h \
             src/libnorn/recstore.h \
             src/libnorn/kademlia.h \
             src/libnorn/log.h \
             src/libnorn/channel.h \
             src/libnorn/replaycache.h \
             src/libnorn/stream.h \
             src/libnorn/streammux.h \
             src/libnorn/transport.h \
             src/libnorn/transport_udp.h \
             src/libnorn/transport_tcp.h \
             src/libnorn/idexch.h \
             src/libnorn/attr.h \
             src/libnorn/sha1.h \
             src/libnorn/net.h

# Static analysis targets
check-cppcheck:
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck --quiet --enable=all --error-exitcode=1 \
			--suppressions-list=$(top_srcdir)/cppcheck.supp \
			--suppress=checkersReport \
			--inline-suppr \
			src/libnorn/*.c src/norn.c; \
	else \
		echo "ERROR: cppcheck not installed. Install cppcheck or skip this check."; \
		exit 1; \
	fi

check-tidy: all
	@if command -v clang-tidy >/dev/null 2>&1; then \
		clang-tidy src/libnorn/*.c src/norn.c -- $(CFLAGS) -I$(top_srcdir)/src/libnorn; \
	else \
		echo "WARNING: clang-tidy not installed. Skipping."; \
	fi

check-format:
	@if command -v clang-format >/dev/null 2>&1; then \
		FAILED=0; \
		for f in src/libnorn/*.c src/libnorn/*.h src/norn.c; do \
			clang-format --dry-run --Werror $$f 2>/dev/null || FAILED=1; \
		done; \
		if [ $$FAILED -eq 1 ]; then \
			echo "Code not formatted. Run: make format"; \
			exit 1; \
		fi; \
	else \
		echo "WARNING: clang-format not installed. Skipping."; \
	fi

format:
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format -i src/libnorn/*.c src/libnorn/*.h src/norn.c; \
		echo "Formatted source files"; \
	else \
		echo "ERROR: clang-format not installed. Install clang-format first."; \
		exit 1; \
	fi