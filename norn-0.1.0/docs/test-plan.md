# Test Plan — 100% Coverage

> Companion to FEAT-001: Unit Test Coverage to 100%

## Current State

| Module | Test File | Line Coverage | Branch Coverage | Status |
|--------|-----------|---------------|-----------------|--------|
| norn.c | test_norn.c | ~30% | ~20% | Incomplete |
| bep44.c | test_bep44.c | ~60% | ~50% | Incomplete |
| sha1.c | test_bep44.c | ~70% | ~60% | Incomplete |
| dhtstore.c | test_dhtstore.c | ~50% | ~40% | Incomplete |
| bencode.c | — | 0% | 0% | Missing |
| crypto.c | — | 0% | 0% | Missing |
| net.c | — | 0% | 0% | Missing |
| kademlia.c | — | 0% | 0% | Missing |
| recstore.c | — | 0% | 0% | Missing |
| log.c | — | 0% | 0% | Missing |
| mainline.c | — | 0% | 0% | Missing |
| norn_impl.c | — | 0% | 0% | Missing |

**Legend:**
- 🟢 Covered
- 🟡 Partial
- 🔴 Missing
- ⚪ Not tracked

---

## Test Files to Create

### 1. tests/test_bencode.c

**Purpose:** Test bencoding codec (lists, dicts, integers, strings)

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_encode_int` | Encode integers | `bencode_int_new`, `bencode_encode` |
| `test_encode_string` | Encode strings | `bencode_string_new`, `bencode_encode` |
| `test_encode_list` | Encode lists | `bencode_list_new`, `bencode_list_add` |
| `test_encode_dict` | Encode dictionaries | `bencode_dict_new`, `bencode_dict_add` |
| `test_decode_int` | Decode integers | `bencode_decode` int case |
| `test_decode_string` | Decode strings | `bencode_decode` string case |
| `test_decode_list` | Decode lists | `bencode_decode` list case, recursion |
| `test_decode_dict` | Decode dictionaries | `bencode_decode` dict case |
| `test_decode_empty` | Empty input | NULL check, len=0 |
| `test_decode_truncated` | Truncated input | `len < expected` |
| `test_decode_malformed` | Invalid bencode | Missing 'e', invalid chars |
| `test_decode_overflow` | Integer overflow | `> INT64_MAX` |
| `test_decode_depth` | Recursion depth | `> BENCODE_MAX_DEPTH` |
| `test_dict_get` | Get dict value | `bencode_dict_get` |
| `test_dict_get_missing` | Missing key | `bencode_dict_get` NULL |
| `test_free_null` | Free NULL | `bencode_free(NULL)` |
| `test_encode_null` | Encode NULL | `bencode_encode(NULL)` |
| `test_roundtrip` | Encode/decode cycle | All types |

**Lines:** ~300

---

### 2. tests/test_crypto.c

**Purpose:** Test crypto utilities (signing, verification, hashing)

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_init` | Initialize libsodium | `crypto_init` |
| `test_keypair_new` | Generate keypair | `crypto_keypair_new` |
| `test_keypair_load_save` | Save and load keypair | `crypto_keypair_load`, `crypto_keypair_save` |
| `test_keypair_load_missing` | Load missing file | NULL return |
| `test_keypair_load_truncated` | Load truncated file | Error handling |
| `test_sign_verify` | Sign and verify | `bf_sign`, `bf_verify` |
| `test_sign_null` | Sign NULL message | NULL-safe |
| `test_verify_null` | Verify NULL | NULL-safe |
| `test_verify_wrong_key` | Wrong public key | Verification failure |
| `test_seal_open` | Sealed box | `bf_seal`, `bf_seal_open` |
| `test_seal_null` | Seal NULL | NULL-safe |
| `test_seal_open_null` | Open NULL | NULL-safe |
| `test_hash_name` | Hash name | `bf_hash_name` |
| `test_hash_name_null` | Hash NULL | NULL-safe |
| `test_is_fqdn` | FQDN validation | `bf_is_fqdn` |
| `test_is_fqdn_invalid` | Invalid FQDNs | Leading dot, trailing dot, empty |
| `test_crc32c` | CRC32C checksum | `crypto_crc32c` |
| `test_crc32c_null` | CRC32C NULL | NULL input |
| `test_xor_distance` | XOR distance | `crypto_xor_distance` |
| `test_compare_distance` | Compare distance | `crypto_compare_distance` |
| `test_generate_node_id` | Node ID generation | `crypto_generate_node_id` |

**Lines:** ~200

---

### 3. tests/test_net.c

**Purpose:** Test network layer (UDP socket I/O)

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_init_null` | Init with NULL | `net_init(NULL)` |
| `test_bind` | Bind to port | `net_bind` |
| `test_bind_invalid_port` | Invalid port | Error handling |
| `test_send_receive` | Send and receive | `net_send`, `net_recv` |
| `test_send_null` | Send NULL | NULL-safe |
| `test_recv_timeout` | Receive with timeout | `net_recv` with timeout |
| `test_get_fd` | Get file descriptor | `net_get_fd` |
| `test_cleanup_null` | Cleanup NULL | `net_cleanup(NULL)` |
| `test_resolve` | DNS resolution | `net_resolve` |
| `test_resolve_invalid` | Invalid hostname | NULL return |
| `test_ip_to_str` | IP to string | `net_ip_to_str` |
| `test_str_to_ip` | String to IP | `net_str_to_ip` |

**Lines:** ~150

**Note:** May need to mock socket operations or use loopback.

---

### 4. tests/test_sha1.c

**Purpose:** Test SHA-1 implementation (for DHT IDs)

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_sha1_empty` | Hash empty string | `sha1("", ...)` |
| `test_sha1_short` | Hash short string | `sha1("abc", ...)` |
| `test_sha1_block` | Hash exactly 64 bytes | Block boundary |
| `test_sha1_multi_block` | Hash multiple blocks | >64 bytes |
| `test_sha1_known` | Known test vectors | FIPS 180-1 examples |
| `test_sha1_null` | Hash NULL | NULL-safe |

**Lines:** ~80

**Note:** Already partially tested via `test_bep44.c`.

---

### 5. tests/test_dhtstore.c (Extend)

**Purpose:** Test DHT storage (bounded cache for untrusted items)

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_init` | Initialize store | `dhtstore_init` |
| `test_init_client_only` | Client-only mode | `client_only=1` |
| `test_put_get` | Put and get item | `dhtstore_put`, `dhtstore_get` |
| `test_put_null` | Put NULL | NULL-safe |
| `test_get_null` | Get NULL | NULL-safe |
| `test_put_immutable` | Put immutable item | `dhtstore_put_immutable` |
| `test_get_immutable` | Get immutable | `dhtstore_get_ex` with `immutable_out` |
| `test_seq_monotonic` | Sequence monotonicity | Newer seq replaces older |
| `test_seq_stale` | Stale sequence rejected | Older seq rejected |
| `test_ttl_expiry` | TTL expiry | Items expire after 2h |
| `test_per_ip_cap` | Per-IP cap | Max 32 items per IP |
| `test_budget_eviction` | Budget eviction | LRU eviction |
| `test_budget_exceeded` | Budget exceeded | Reject when full |
| `test_del` | Delete item | `dhtstore_del` |
| `test_del_missing` | Delete missing | `dhtstore_del` returns 0 |
| `test_list` | List items | `dhtstore_list` |
| `test_list_mutable` | List mutable only | `want_immutable=0` |
| `test_list_immutable` | List immutable only | `want_immutable=1` |
| `test_bytes_count` | Track bytes | `dhtstore_bytes`, `dhtstore_count` |
| `test_signature_verify` | Verify signature | `dhtstore_put` verifies sig |
| `test_target_mismatch` | Target mismatch | Reject if target ≠ SHA1(k) |

**Lines:** ~250 (current ~30, extend ~220)

---

### 6. tests/test_kademlia.c

**Purpose:** Test Kademlia routing (XOR distance, node table)

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_xor_distance` | XOR metric | `crypto_xor_distance` |
| `test_compare_distance` | Distance comparison | Closer/farther |
| `test_add_node` | Add node to table | `mainline_add_node` |
| `test_add_node_null` | Add NULL | NULL-safe |
| `test_add_node_self` | Add self | Reject self |
| `test_add_node_duplicate` | Duplicate node | Update last_seen |
| `test_add_node_full` | Table full | Reject when full |
| `test_add_node_subnet_cap` | Subnet cap | Max 8 per /24 |
| `test_evict_stale` | Evict stale nodes | `mainline_evict_stale` |
| `test_evict_stale_none` | No stale nodes | All recent |
| `test_get_node_count` | Node count | `mainline_get_node_count` |
| `test_save_load_nodes` | Persist table | `mainline_save_nodes`, `mainline_load_nodes` |
| `test_save_nodes_error` | Write error | Invalid path |
| `test_load_nodes_invalid` | Load invalid | Bad magic, bad version |
| `test_bootstrap` | Bootstrap | `mainline_bootstrap` |
| `test_needs_bootstrap` | Bootstrap interval | `mainline_needs_bootstrap` |
| `test_find_node` | Find node query | `mainline_find_node` |
| `test_crawl` | Crawl network | `mainline_crawl_network` |

**Lines:** ~200

---

### 7. tests/test_recstore.c

**Purpose:** Test trusted record store

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_accept` | Accept valid record | `recstore_accept` |
| `test_accept_null` | Accept NULL | NULL-safe |
| `test_accept_invalid_sig` | Invalid signature | Reject |
| `test_accept_stale_seq` | Stale sequence | Reject |
| `test_get` | Get record | `recstore_get` |
| `test_get_missing` | Get missing | NULL return |
| `test_list` | List records | `recstore_list` |

**Lines:** ~100

**Note:** Need to check if `recstore.c` exists and what API it provides.

---

### 8. tests/test_log.c

**Purpose:** Test logging module

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_init` | Initialize logging | `log_init` |
| `test_set_level` | Set level | `log_set_level` |
| `test_log_debug` | Log debug | `LOG_DEBUG` |
| `test_log_info` | Log info | `LOG_INFO` |
| `test_log_warn` | Log warn | `LOG_WARN` |
| `test_log_error` | Log error | `LOG_ERROR` |
| `test_log_level_filter` | Level filtering | Below min_level filtered |
| `test_log_null_sink` | NULL sink | Write to stderr |
| `test_log_format` | Output format | Timestamp, level, file, line |
| `test_log_long_message` | Long message | Truncation |
| `test_log_format_string` | Format string | `%d`, `%s` |
| `test_log_null_format` | NULL format | Don't crash |
| `test_shutdown` | Shutdown | `log_shutdown` |

**Lines:** ~150

---

### 9. tests/test_norn.c (Extend)

**Purpose:** Test public API

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_new_free` | Create and free client | `norn_new`, `norn_free` |
| `test_new_null` | New with NULL | NULL-safe |
| `test_free_null` | Free NULL | NULL-safe |
| `test_get_id` | Get node ID | `norn_get_id` |
| `test_get_id_null` | Get ID NULL | NULL-safe |
| `test_bootstrap` | Bootstrap | `norn_bootstrap` |
| `test_bootstrap_null` | Bootstrap NULL | NULL-safe |
| `test_tick` | Process transactions | `norn_tick` |
| `test_tick_null` | Tick NULL | NULL-safe |
| `test_get_fd` | Get socket FD | `norn_get_fd` |
| `test_get_fd_null` | Get FD NULL | NULL-safe |
| `test_put_mutable` | Put mutable | `norn_put_mutable` |
| `test_put_mutable_null` | Put NULL | NULL-safe |
| `test_get_mutable` | Get mutable | `norn_get_mutable` |
| `test_get_mutable_null` | Get NULL | NULL-safe |
| `test_put_immutable` | Put immutable | `norn_put_immutable` |
| `test_get_immutable` | Get immutable | `norn_get_immutable` |
| `test_announce` | Announce peer | `norn_announce` |
| `test_discover` | Discover peers | `norn_discover` |
| `test_encode_decode_mutable` | Encode/decode | `norn_encode_mutable`, `norn_decode_mutable` |

**Lines:** ~200 (current ~40, extend ~160)

---

### 10. tests/test_mainline.c

**Purpose:** Test mainline DHT protocol implementation

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_init_cleanup` | Init and cleanup | `mainline_init`, `mainline_cleanup` |
| `test_init_null` | Init NULL | NULL-safe |
| `test_cleanup_null` | Cleanup NULL | NULL-safe |
| `test_process_packet` | Process packet | `mainline_process_packet` |
| `test_process_packet_null` | NULL packet | NULL-safe |
| `test_process_packet_malformed` | Malformed packet | Invalid bencode |
| `test_add_bootstrap` | Add bootstrap peer | `mainline_add_bootstrap` |
| `test_set_private` | Set private mode | `mainline_set_private` |
| `test_set_read_only` | Set read-only mode | `mainline_set_read_only` |
| `test_peer_cache` | Peer cache | `peer_cache_put`, `peer_cache_get` |
| `test_peer_cache_evict` | Cache eviction | LRU |
| `test_peer_cache_save_load` | Persist cache | `peer_cache_save`, `peer_cache_load` |
| `test_lookup` | DHT lookup | `mainline_lookup` |
| `test_lookup_mutable` | Mutable lookup | `mainline_lookup_mutable` |
| `test_resolve_node` | Resolve node | `mainline_resolve_node` |

**Lines:** ~200

**Note:** May need to mock network or use loopback.

---

## Test Categories

### NULL Input Tests

Every public function must handle NULL inputs gracefully:

```c
/* Pattern */
assert(func(NULL, valid_arg) == -1 || func(NULL, valid_arg) == NULL);
assert(func(valid_arg, NULL) == -1 || func(valid_arg, NULL) == NULL);
```

Apply to:
- All `norn_*` functions
- All `bep44_*` functions
- All `dhtstore_*` functions
- All `log_*` functions
- All `crypto_*` functions

### Buffer Overflow Tests

Undersized buffers must be rejected, not truncated:

```c
/* Pattern */
unsigned char tiny[1];
int len = encode(tiny, sizeof(tiny), valid_input);
assert(len == -1);  /* Must fail, not truncate */
```

Apply to:
- `bep44_encode` — output buffer
- `bep44_record_encode` — output buffer
- `bep44_signbuf` — output buffer
- `dhtstore_get` — output buffer

### Boundary Tests

| Boundary | Test |
|----------|------|
| Empty string | `len=0` |
| Max size | `len=1000` (BEP-44 limit) |
| Over max | `len=1001` — reject |
| TTL expiry | Mock time, test expiry |
| Per-IP cap | Fill to limit, then reject |
| Budget | Fill to budget, then evict |

### Error Path Tests

| Error | Test |
|-------|------|
| Invalid input | Malformed bencode, bad signature |
| Resource exhaustion | Table full, budget exceeded |
| Network error | Timeout, unreachable |
| File I/O error | Missing file, permission denied |

---

## Coverage Tracking

Files in `tests/coverage-tracked.txt`:

```
# Coverage-tracked sources for norn
# 100% line AND branch coverage required before commit

norn.c
client.c
bep44.c
sha1.c
dhtstore.c
recstore.c
bencode.c
crypto.c
net.c
log.c
kademlia.c
mainline.c
```

### LCOV_EXCL_LINE

Use sparingly for genuinely untestable code:

```c
/* LCOV_EXCL_LINE: allocation failure in cold path */
if (malloc(size) == NULL) {
    return -1;  /* LCOV_EXCL_LINE */
}
```

---

## Test Execution

### Unit Tests

```bash
# Run all unit tests
make check

# Run with coverage
./configure --enable-coverage CFLAGS="-O0 -g"
make clean
make
make check
make coverage

# Expected output
PASS: norn.c - Lines: 100%, Branches: 100%
PASS: bep44.c - Lines: 100%, Branches: 100%
...
PASS: log.c - Lines: 100%, Branches: 100%
Coverage gate PASSED
```

### System Integration Tests (SIT)

SIT tests verify build, install, and CLI operations in an isolated container.

```bash
# Run SIT tests locally (requires bats)
bats tests/sit/*.bats

# Run SIT tests in container (requires podman)
./tests/sit/run.sh
```

**SIT Test Suites:**

| Suite | File | Tests | Purpose |
|-------|------|-------|---------|
| Build | `build.bats` | 12 | Build, install, pkg-config |
| Coverage | `coverage.bats` | 8 | Coverage gate enforcement |
| CLI | `cli.bats` | 20 | CLI commands and options |

**SIT Requirements:**

- Alpine Linux container (podman)
- bats testing framework
- Network access for dependency installation

### Performance Integration Tests (PIT)

PIT tests verify network operations against real DHT nodes.

```bash
# Run PIT tests locally (requires network access)
bats tests/pit/*.bats

# Run PIT tests in container with network access
./tests/pit/run.sh

# Skip PIT tests (CI without network)
SKIP_PIT=1 ./tests/pit/run.sh
```

**PIT Test Suites:**

| Suite | File | Tests | Purpose |
|-------|------|-------|---------|
| Network | `network.bats` | 12 | Real DHT operations |

**PIT Requirements:**

- Network connectivity to mainline DHT bootstrap nodes
- `router.bittorrent.com:6881` reachable
- No firewall blocking UDP 6881

**PIT Test Categories:**

1. **Daemon lifecycle** — Start, bind to port, graceful shutdown
2. **Bootstrap** — Reach DHT routers, join network
3. **Get operations** — Retrieve records from real DHT
4. **Set operations** — Store records to real DHT
5. **Round-trip** — Set then get same record
6. **Concurrency** — Multiple concurrent operations
7. **Graceful shutdown** — Handle SIGTERM, SIGINT
8. **Modes** — Read-only mode, custom bootstrap peers

---

### 11. tests/test_norn_async.c (New)

**Purpose:** Test async API (critical)

| Test Case | Description | Covers |
|-----------|-------------|--------|
| `test_get_mutable_async` | Async get returns immediately | Non-blocking |
| `test_get_mutable_callback` | Callback invoked on response | Callback dispatch |
| `test_get_immutable_async` | Async get immutable | Non-blocking |
| `test_discover_async` | Async peer discovery | Non-blocking |
| `test_bootstrap_async` | Async bootstrap | Non-blocking |
| `test_tick_no_packets` | Tick with no packets | Non-blocking poll |
| `test_tick_null` | Tick with NULL client | NULL-safe |
| `test_get_fd` | Get FD for event loop | Event loop integration |
| `test_get_fd_null` | Get FD with NULL client | NULL-safe |
| `test_multiple_pending` | Multiple pending requests | Transaction queue |
| `test_transaction_timeout` | Transaction expires | Timeout handling |
| `test_callback_user_data` | Callback receives user_data | User data passing |

**Lines:** ~150

**Note:** Requires async API implementation (FEAT-012).

| Module | New Test File | New Tests | Extended Tests | Total Tests |
|--------|---------------|-----------|----------------|-------------|
| bencode | test_bencode.c | 18 | — | 18 |
| crypto | test_crypto.c | 21 | — | 21 |
| net | test_net.c | 11 | — | 11 |
| sha1 | (in test_bep44.c) | 6 | 0 | 6 |
| dhtstore | — | 20 | 20 | 21 |
| kademlia | test_kademlia.c | 16 | — | 16 |
| recstore | test_recstore.c | 7 | — | 7 |
| log | test_log.c | 13 | — | 13 |
| norn | — | 21 | 16 | 22 |
| mainline | test_mainline.c | 14 | — | 14 |
| bep44 | (exists) | 0 | 5 | 7 |
| **Total Unit** | **8 new** | **147** | **41** | **188** |

| Suite | File | Tests | Purpose |
|-------|------|-------|---------|
| SIT Build | `tests/sit/build.bats` | 12 | Build, install, pkg-config |
| SIT Coverage | `tests/sit/coverage.bats` | 8 | Coverage gate enforcement |
| SIT CLI | `tests/sit/cli.bats` | 20 | CLI commands and options |
| PIT Network | `tests/pit/network.bats` | 12 | Real DHT operations |
| **Total Integration** | **4 suites** | **52** | |

| Level | Tests | Execution |
|-------|-------|-----------|
| Unit | 188 | `make check` |
| SIT | 40 | `bats tests/sit/*.bats` |
| PIT | 12 | `bats tests/pit/*.bats` (network required) |
| **Total** | **240** | |

---

## Priority Order

1. **High priority** (MILESTONE-0.2.0):
   - `test_log.c` — Required by FEAT-002
   - `test_bencode.c` — Critical for DHT protocol
   - `test_crypto.c` — Security-critical
   - Extend `test_dhtstore.c` — Core storage
   - Extend `test_norn.c` — Public API

2. **Medium priority** (MILESTONE-0.4.0):
   - `test_kademlia.c` — Routing logic
   - `test_mainline.c` — Protocol implementation

3. **Low priority** (as needed):
   - `test_net.c` — Network layer (may require mocking)
   - `test_recstore.c` — Record store
   - `test_sha1.c` — Already partially tested