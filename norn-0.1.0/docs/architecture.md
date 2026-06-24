# norn Architecture

## Overview

norn is a mainline DHT client library for P2P peer discovery and bootstrap. Named after the Norns of Nordic mythology who control destiny, it provides:

- **DHT operations**: Store and retrieve mutable/immutable items (BEP-44)
- **Peer discovery**: Find peers by info_hash
- **Bootstrap**: Join the DHT network
- **In-memory operation**: No configuration files, no disk I/O

## Design Philosophy

1. **Single-threaded, event-loop compatible** — No blocking operations
2. **No heap allocations in hot paths** — Arena/pool allocators for performance
3. **100% testable in isolation** — All modules unit-testable without network
4. **Bounded memory** — Configurable budgets, predictable resource usage
5. **Security-first** — Signed records, replay protection, rate limiting

## Module Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         Application                              │
│                   (bifrost, CLI, etc.)                           │
└─────────────────────────────┬───────────────────────────────────┘
                              │ norn.h API
┌─────────────────────────────▼───────────────────────────────────┐
│                          norn.c                                  │
│                   (Public API, coordination)                     │
└─────┬───────────┬───────────┬───────────────┬──────────────────┘
      │           │           │               │
      │           │           │               │
┌─────▼─────┐ ┌───▼────┐ ┌────▼─────┐ ┌──────▼─────────────────────┐
│ norn_impl │ │ channel │ │ idexch   │ │ stream.c/streammux.c      │
│ (async)   │ │(secure) │ │(key exch)│ │ (reliable ordered stream) │
└─────┬─────┘ └────┬───┘ └────┬─────┘ └──────────┬────────────────┘
      │           │           │                  │
      │           │           │                  │
      │      ┌────▼────────┐  │                  │
      │      │ replaycache │  │                  │
      │      │ (anti-replay)│  │                  │
      │      └─────────────┘  │                  │
      │                       │                  │
┌─────▼───────────────────────▼──────────────────▼───────────────┐
│                         transport.c                             │
│                    (transport abstraction)                      │
└─────┬──────────────────────┬───────────────────────────────────┘
      │                      │
┌─────▼────────┐      ┌──────▼─────────┐
│transport_udp │      │ transport_tcp  │
│  (UDP I/O)   │      │   (TCP I/O)    │
└─────┬────────┘      └──────┬─────────┘
      │                      │
┌─────▼──────────────────────▼───────────────────────────────────┐
│                            net.c                                 │
│                     (socket operations)                          │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                     Supporting Modules                           │
├─────────────────────────────────────────────────────────────────┤
│ bep44.c          BEP-44 mutable/immutable items (signing, etc)  │
│ dhtstore.c       Untrusted DHT item storage (bounded cache)      │
│ recstore.c       Trusted record storage (our own records)       │
│ kademlia.c       Kademlia routing table (XOR distance, buckets)  │
│ crypto.c         Cryptographic utilities (sign, verify, hash)    │
│ sha1.c           SHA-1 for DHT IDs                               │
│ bencode.c        Bencoding codec (lists, dicts, integers, etc)   │
│ log.c            Structured logging                               │
│ attr.c           Key-value attribute store                        │
└─────────────────────────────────────────────────────────────────┘
```

## Layer Responsibilities

### Layer 1: Public API (norn.h)

**Purpose:** Stable ABI for applications

**Module:** `norn.c`

| Function | Purpose |
|----------|---------|
| `norn_new` / `norn_free` | Client lifecycle |
| `norn_bootstrap` | Join DHT network |
| `norn_put_mutable` | Store mutable signed record |
| `norn_get_mutable` | Retrieve mutable record (async) |
| `norn_put_immutable` | Store content-addressed value |
| `norn_get_immutable` | Retrieve immutable value (async) |
| `norn_announce` | Announce peer for info_hash |
| `norn_discover` | Find peers for info_hash (async) |
| `norn_tick` | Process pending transactions |

**Thread Safety:** All functions are single-threaded. Caller must synchronize if using from multiple threads.

**Memory:** Client allocates `norn_client_t`, all other memory managed internally.

### Layer 2: Coordination (norn_impl.c)

**Purpose:** Async operation management

**Module:** `norn_impl.c`

| Function | Purpose |
|----------|---------|
| Transaction tracking | Pending DHT requests |
| Timeout handling | Retry expired requests |
| Callback dispatch | Notify application of results |

### Layer 3: Protocol Implementation

#### channel.c — Secure Channel

**Purpose:** End-to-end encrypted communication

| Function | Purpose |
|----------|---------|
| `channel_new` | Create secure channel |
| `channel_seal` | Encrypt + authenticate |
| `channel_open` | Decrypt + verify |
| `channel_peer_finished` | Check for FIN |

**Security:** Uses libsodium's `crypto_box` for authenticated encryption.

#### idexch.c — Identity Exchange

**Purpose:** Authenticated key exchange

| Function | Purpose |
|----------|---------|
| `idexch_new` | Create exchange context |
| `idexch_offer` | Generate offer message |
| `idexch_accept` | Accept offer, generate response |
| `idexch_finish` | Complete exchange |

**Security:** Noise Protocol Framework pattern, Ed25519 keys.

#### stream.c — Reliable Ordered Stream

**Purpose:** TCP-like stream over unreliable datagrams

| Function | Purpose |
|----------|---------|
| `stream_new` | Create stream |
| `stream_write` | Queue data for delivery |
| `stream_input` | Process incoming segment |
| `stream_read` | Read delivered data |
| `stream_tick` | Drive timers |

**Implementation:** 
- Cumulative ACK with SACK bitmap
- Adaptive RTO (Jacobson/Karels algorithm)
- Fast retransmit on duplicate ACKs

### Layer 4: Transport Abstraction

#### transport.c — Abstract Transport

**Purpose:** Protocol-agnostic transport interface

| Function | Purpose |
|----------|---------|
| `transport_send` | Send datagram |
| `transport_recv` | Receive datagram |

#### transport_udp.c — UDP Transport

**Purpose:** UDP socket I/O

#### transport_tcp.c — TCP Transport

**Purpose:** TCP socket I/O (for stream-based protocols)

### Layer 5: Network Operations

#### net.c — Socket Layer

**Purpose:** Low-level socket operations

| Function | Purpose |
|----------|---------|
| `net_bind` | Bind UDP socket |
| `net_send` | Send datagram |
| `net_recv` | Receive datagram |
| `net_resolve` | DNS resolution |

### Supporting Modules

#### bep44.c — BEP-44 Implementation

**Purpose:** Mutable and immutable DHT items

| Function | Purpose |
|----------|---------|
| `bep44_target` | Compute DHT key from public key |
| `bep44_encode` | Sign and encode mutable item |
| `bep44_decode` | Verify and decode mutable item |
| `bep44_immutable_target` | Compute key from value (SHA1) |

#### dhtstore.c — Untrusted Storage

**Purpose:** Bounded cache for untrusted DHT items

| Function | Purpose |
|----------|---------|
| `dhtstore_init` | Set byte budget |
| `dhtstore_put` | Store verified item |
| `dhtstore_get` | Retrieve by target |
| `dhtstore_del` | Remove item |

**Security:**
- Per-IP rate limiting (max 32 items)
- TTL expiry (2 hours)
- LRU eviction when budget exceeded
- Signature verification before storage

#### recstore.c — Trusted Storage

**Purpose:** Our own signed records

| Function | Purpose |
|----------|---------|
| `recstore_accept` | Verify and store trusted record |
| `recstore_get` | Retrieve trusted record |

**Difference from dhtstore:** Only accepts records we signed ourselves.

#### kademlia.c — Routing Table

**Purpose:** DHT node management

| Function | Purpose |
|----------|---------|
| `kad_init` | Initialize routing table |
| `kad_update_node` | Add/update node |
| `kad_get_bucket_index` | Find bucket for node ID |

#### crypto.c — Cryptographic Utilities

**Purpose:** Ed25519 operations, hashing

| Function | Purpose |
|----------|---------|
| `crypto_keypair_new` | Generate keypair |
| `bf_sign` / `bf_verify` | Ed25519 sign/verify |
| `bf_seal` / `bf_seal_open` | Anonymous sealed box |
| `bf_hash_name` | Hash FQDN to key |
| `crypto_crc32c` | CRC32C checksum |

#### bencode.c — Bencoding Codec

**Purpose:** Parse/encode BitTorrent bencoding

| Function | Purpose |
|----------|---------|
| `bencode_decode` | Parse bencoded data |
| `bencode_encode` | Encode to bencoded format |
| `bencode_dict_get` | Get dict value by key |

#### log.c — Logging

**Purpose:** Structured logging with levels

| Function | Purpose |
|----------|---------|
| `log_init` | Set log level |
| `LOG_DEBUG`, `LOG_INFO`, etc. | Level-specific logging |

#### attr.c — Key-Value Store

**Purpose:** Generic attribute storage

| Function | Purpose |
|----------|---------|
| `attr_set` | Set attribute |
| `attr_get` | Get attribute |

## Data Flow

### Bootstrap Sequence

```
Application              norn.c              net.c            DHT Router
    │                     │                   │                    │
    │ norn_bootstrap()    │                   │                    │
    ├────────────────────►│                   │                    │
    │                     │ find_node query   │                    │
    │                     ├──────────────────►│                    │
    │                     │                   │ UDP send           │
    │                     │                   ├───────────────────►│
    │                     │                   │                    │
    │                     │                   │   UDP recv         │
    │                     │                   │◄───────────────────┤
    │                     │ find_node response│                    │
    │                     │◄──────────────────┤                    │
    │                     │                   │                    │
    │                     │ add_node to table│                    │
    │                     ├──────────────────►│                    │
    │                     │                   │                    │
    │   return 0          │                   │                    │
    │◄────────────────────┤                   │                    │
```

### Put Mutable Item

```
Application              norn.c            bep44.c           DHT Nodes
    │                     │                   │                    │
    │ norn_put_mutable()  │                   │                    │
    ├────────────────────►│                   │                    │
    │                     │ bep44_encode()    │                    │
    │                     ├──────────────────►│                    │
    │                     │   signed record   │                    │
    │                     │◄──────────────────┤                    │
    │                     │                   │                    │
    │                     │ put request to    │                    │
    │                     │ each node in table│                    │
    │                     ├──────────────────────────────────────►│
    │                     │                   │                    │
    │                     │   put response    │                    │
    │                     │◄──────────────────────────────────────┤
    │                     │                   │                    │
    │   return 0          │                   │                    │
    │◄────────────────────┤                   │                    │
```

### Get Mutable Item (Async)

```
Application              norn.c            dhtstore.c         DHT Nodes
    │                     │                   │                    │
    │ norn_get_mutable()  │                   │                    │
    ├────────────────────►│                   │                    │
    │                     │ check dhtstore    │                    │
    │                     ├──────────────────►│                    │
    │                     │   not found       │                    │
    │                     │◄──────────────────┤                    │
    │                     │                   │                    │
    │                     │ create transaction│                   │
    │                     │ store callback    │                   │
    │                     │                   │                    │
    │                     │ get request to    │                   │
    │                     │ closest nodes     │                   │
    │                     ├──────────────────────────────────────►│
    │                     │                   │                    │
    │   return 0          │                   │                    │
    │◄────────────────────┤                   │                    │
    │                     │                   │                    │
    │   ... later ...     │                   │                    │
    │                     │                   │                    │
    │ norn_tick()         │                   │                    │
    ├────────────────────►│                   │                    │
    │                     │ process response   │                    │
    │                     │ verify signature   │                    │
    │                     │ store in dhtstore │                    │
    │                     ├──────────────────►│                    │
    │                     │                   │                    │
    │                     │ invoke callback   │                    │
    ├─────────────────────┤                   │                    │
    │ callback(value)     │                   │                    │
    │◄────────────────────┤                   │                    │
```

## Threading Model

**Single-threaded, event-loop driven.**

All operations are non-blocking:

1. **Application calls norn_bootstrap()** — Schedules bootstrap queries, returns immediately
2. **Application polls norn_tick()** — Processes received packets, invokes callbacks
3. **Application calls norn_get_fd()** — Gets socket FD for poll()/select()
4. **Application receives callback** — Results delivered via callback in norn_tick()

**Rationale:**
- Fits into any event loop (libevent, libuv, custom)
- No threading primitives needed
- Deterministic execution
- Easy to test without network

**Memory Safety:**
- No global state
- All state in `norn_client_t`
- Callbacks receive user_data pointer
- Application controls allocation lifetime

## Memory Management

### Arena Pattern

Hot paths use arena/pool allocators:

```c
/* Example: stream.c */
typedef struct {
    unsigned char arena[STREAM_ARENA_SIZE];
    size_t arena_used;
} stream_t;

void *stream_alloc(stream_t *s, size_t len) {
    if (s->arena_used + len > sizeof(s->arena)) return NULL;
    void *ptr = s->arena + s->arena_used;
    s->arena_used += len;
    return ptr;
}
```

**Benefits:**
- No malloc/free in hot path
- Cache-friendly (contiguous memory)
- Bounded memory usage
- No fragmentation

### Bounded Buffers

All modules have size limits:

| Module | Limit | Rationale |
|--------|-------|-----------|
| dhtstore | Configurable budget (default: RAM/512) | Prevent untrusted data exhaustion |
| stream | 64KB send buffer | TCP-like window |
| bep44 | 1000 bytes | BEP-44 spec limit |
| bencode | 1MB | Max message size |

### Zero-Copy Paths

Where possible, data is passed by reference:

```c
/* dhtstore doesn't copy value data */
int dhtstore_put(..., const unsigned char *v, size_t vlen, ...) {
    /* Store reference, not copy */
    item->v = v;
    item->vlen = vlen;
}
```

**Caveat:** Caller must ensure value remains valid until item expires.

## Security Model

### Threats and Mitigations

| Threat | Mitigation | Module |
|--------|-----------|--------|
| **Replay attacks** | Sequence numbers, timestamp validation | bep44.c, recstore.c |
| **Resource exhaustion** | Per-IP rate limits, byte budgets | dhtstore.c |
| **Data corruption** | Ed25519 signatures, SHA-1 hashes | crypto.c, bep44.c |
| **Man-in-the-middle** | Authenticated encryption (crypto_box) | channel.c |
| **Node impersonation** | Node ID = SHA256(pubkey) | crypto.c |
| **Memory exhaustion** | Bounded buffers, arena allocators | All modules |
| **Timing attacks** | Constant-time crypto comparisons | crypto.c |

### Record Verification Flow

```
Untrusted Item Received
         │
         ▼
┌─────────────────────────┐
│  Verify signature?      │
│  bf_verify(sig, v, pk)  │
└─────────┬───────────────┘
          │
          ├──── No ────► Reject
          │
         Yes
          │
          ▼
┌─────────────────────────┐
│  Check sequence?        │
│  seq > stored_seq?      │
└─────────┬───────────────┘
          │
          ├──── No ────► Reject (stale/replay)
          │
         Yes
          │
          ▼
┌─────────────────────────┐
│  Check target?          │
│  target == SHA1(pk)?    │
└─────────┬───────────────┘
          │
          ├──── No ────► Reject (target mismatch)
          │
         Yes
          │
          ▼
┌─────────────────────────┐
│  Check per-IP cap?      │
│  items[ip] < MAX?      │
└─────────┬───────────────┘
          │
          ├──── No ────► Reject (rate limit)
          │
         Yes
          │
          ▼
┌─────────────────────────┐
│  Check budget?          │
│  bytes + vlen < budget? │
└─────────┬───────────────┘
          │
          ├──── No ────► Evict LRU, then check again
          │
         Yes
          │
          ▼
      Store Item
```

### Defense in Depth

1. **Layer 1: Network** — Per-IP rate limiting (net.c)
2. **Layer 2: Protocol** — Signature verification (bep44.c)
3. **Layer 3: Application** — Sequence monotonicity (dhtstore.c, recstore.c)
4. **Layer 4: Resource** — Byte budgets, TTL expiry (dhtstore.c)

## Performance Characteristics

### Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| DHT lookup | O(log N) | Kademlia XOR distance |
| Node insert | O(1) | Bucket-based |
| Item store | O(1) | Hash table |
| Item retrieve | O(1) | Hash table |
| Stream send | O(1) | Per segment |
| Stream receive | O(log N) | SACK bitmap lookup |

### Memory Footprint

| Component | Size | Notes |
|-----------|------|-------|
| norn_client_t | ~10KB | Base overhead |
| Routing table | ~2KB | 256 buckets × 8 nodes |
| dhtstore | Configurable | Default: RAM/512 |
| stream | ~70KB | 64KB buffer + metadata |
| channel | ~1KB | Per connection |

### Throughput

On modern hardware (Apple M1, Linux x86_64):

| Operation | Ops/sec | Notes |
|-----------|---------|-------|
| bep44_encode | ~500,000 | Sign + encode |
| bep44_decode | ~1,000,000 | Verify + decode |
| dhtstore_put | ~2,000,000 | Hash insert |
| stream_write | ~10,000,000 | Segment queue |
| crypto_sign | ~100,000 | Ed25519 |

## Error Handling

All public functions return:

- **int**: `0` for success, `-1` for error
- **pointer**: Valid pointer or `NULL` on error

**Example:**

```c
int norn_get_mutable(norn_client_t *client, ...) {
    if (!client) return -1;  /* NULL-safe */
    if (!callback) return -1;
    
    /* ... */
    return 0;  /* Success */
}
```

**Pattern:** Always check for NULL inputs, return error codes, never crash.

## Testing Strategy

See [test-plan.md](test-plan.md) for details.

- **Unit tests**: All modules, 100% line and branch coverage
- **SIT**: Build/install/CLI in containers
- **PIT**: Network operations against real DHT nodes

## Portability

**Supported platforms:**
- Linux: Alpine, Debian, Ubuntu, Fedora, RHEL, CentOS, Arch, OpenSUSE
- macOS: Homebrew, MacPorts
- FreeBSD: Ports

**Dependencies:**
- C99 compiler
- libsodium >= 1.0.0 (for Ed25519, X25519, crypto_box)
- POSIX (for socket operations)

**Build system:**
- autoconf, automake, libtool
- `./autogen.sh && ./configure && make && make check`

## References

- [BEP-5: DHT Protocol](BEP-REFERENCES.md#bep-5)
- [BEP-44: Mutable/Immutable Items](BEP-REFERENCES.md#bep-44)
- [BEP-43: Read-Only Nodes](BEP-REFERENCES.md#bep-43)
- [PORTING.md](PORTING.md) — Integration guide