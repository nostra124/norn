# BEP References

This document summarizes the BitTorrent Enhancement Proposals (BEPs) implemented by norn.

## BEP-5: DHT Protocol

**Title:** DHT Protocol  
**Status:** Accepted  
**URL:** <http://www.bittorrent.org/beps/bep_0005.html>

### Summary

BEP-5 defines the Distributed Hash Table (DHT) used by BitTorrent clients for
peer discovery without a central tracker. The DHT uses Kademlia's XOR distance
metric to locate peers.

### Key Concepts

- **Node ID:** 160-bit identifier (SHA-1 hash)
- **XOR Distance:** Distance metric `d(A, B) = A XOR B`
- **Routing Table:** Buckets organized by XOR distance
- **K-Bucket:** Up to 8 nodes per bucket
- **Lookup:** Iteratively query closest nodes

### norn Implementation

- `kademlia.c`: Kademlia routing table
- `norn.c`: DHT client API
- `net.c`: UDP socket operations

### Operations

| Operation | Description | norn Function |
|-----------|-------------|---------------|
| `ping` | Check if node is alive | `net.c` |
| `find_node` | Find node by ID | `norn_bootstrap` |
| `get_peers` | Find peers for info_hash | `norn_discover` |
| `announce_peer` | Announce self as peer | `norn_announce` |

### Routing Table

```
Bucket 0:  Nodes with distance 2^159 to 2^160 from self
Bucket 1:  Nodes with distance 2^158 to 2^159 from self
...
Bucket 159: Nodes with distance 1 to 2 from self
```

### Node ID Generation

```c
node_id = SHA256(public_key)[:20]  // First 20 bytes of SHA-256
```

### Security

- **Token security:** Tokens prevent spoofed announcements
- **Node ID restrictions:** Cannot choose arbitrary IDs
- **Rate limiting:** Per-IP limits prevent abuse

---

## BEP-44: Mutable and Immutable Items

**Title:** Mutable and Immutable Items  
**Status:** Accepted  
**URL:** <http://www.bittorrent.org/beps/bep_0044.html>

### Summary

BEP-44 extends the DHT to store arbitrary data, not just peer lists. Items can be:

- **Mutable:** Signed with Ed25519 key, can be updated by key holder
- **Immutable:** Content-addressed (SHA-1 hash), cannot be changed

### Key Concepts

- **Target:** DHT key = SHA-1("k" || public_key) for mutable, SHA-1(bencode(v)) for immutable
- **Signature:** Ed25519 signature over canonical buffer
- **Sequence Number:** Monotonically increasing, prevents replay attacks
- **Salt:** Optional value for creating multiple items under same key

### norn Implementation

- `bep44.c`: BEP-44 encoding/decoding
- `dhtstore.c`: DHT item storage
- `recstore.c`: Trusted record storage

### Mutable Items

```
put:
  target = SHA-1("k" || public_key)
  value = <arbitrary bytes, max 1000>
  seq = <monotonically increasing integer>
  sig = ed25519_sign(canonical_buffer, secret_key)

canonical_buffer = "3:seqi" + seq + "e1:v" + vlen + ":" + value

get:
  query: {"a": {"target": <20 bytes>, "seq": <expected seq>}, "q": "get", "y": "q"}
  response: {"r": {"k": <32 bytes>, "seq": <seq>, "v": <value>, "sig": <64 bytes>}}
```

### Immutable Items

```
put:
  target = SHA-1(bencode(value))
  value = <arbitrary bytes, max 1000>

get:
  query: {"a": {"target": <20 bytes>}, "q": "get", "y": "q"}
  response: {"r": {"v": <value>}}
```

### Canonical Buffer

The canonical buffer for signing is:

```
For unsalted items:
  "3:seqi<seq>e1:v<vlen>:<value>"

For salted items:
  "4:salt<saltlen>:<salt>3:seqi<seq>e1:v<vlen>:<value>"
```

### Value Size Limit

- **Maximum:** 1000 bytes
- **Reason:** Prevent DHT nodes from being overwhelmed
- **norn enforces:** Returns error for values > 1000 bytes (BUG-059)

### Sequence Numbers

- Must be monotonically increasing for same public key
- Prevents replay attacks
- `dhtstore.c` rejects stale sequence numbers

### Salt Usage

- Creates multiple items under same public key
- Common use: per-name signed keys (spub/sget)
- `target = SHA-1(public_key || salt)`

### norn API

| Function | Description |
|----------|-------------|
| `norn_put_mutable` | Store mutable signed item |
| `norn_get_mutable` | Retrieve mutable item (async) |
| `norn_put_immutable` | Store immutable content-addressed item |
| `norn_get_immutable` | Retrieve immutable item (async) |
| `bep44_encode` | Sign and encode mutable item |
| `bep44_decode` | Verify and decode mutable item |
| `bep44_target` | Compute DHT key from public key |
| `bep44_immutable_target` | Compute DHT key from value |

---

## BEP-43: Read-Only DHT Nodes

**Title:** Read-Only DHT Nodes  
**Status:** Accepted  
**URL:** <http://www.bittorrent.org/beps/bep_0043.html>

### Summary

BEP-43 defines "read-only" DHT nodes that do not respond to queries or store
data from other nodes. These nodes participate in the DHT for peer discovery
but contribute no resources.

### Key Concepts

- **Read-Only Mode:** Node does not respond to DHT queries
- **Resource Conservation:** Useful for low-bandwidth devices
- **Privacy:** Reduces exposure to malicious nodes

### norn Implementation

- `norn_config_t.read_only`: Set to 1 for read-only mode
- `net.c`: Ignores incoming queries when read-only

### Use Cases

1. **Low-bandwidth devices:** Mobile phones, embedded systems
2. **Privacy-focused clients:** Reduce attack surface
3. **Client-only mode:** Only need peer discovery, not DHT storage

### Configuration

```c
norn_config_t cfg = {
    .version = "myapp/1.0",
    .read_only = 1  // Enable read-only mode
};
norn_client_t *client = norn_new(pubkey, secret, &cfg);
```

### Behavior

When `read_only = 1`:

- Node does not respond to `ping`, `find_node`, `get_peers`
- Node still sends queries (bootstrap, lookup)
- Node does not store DHT items (`dhtstore` not used)
- Node still receives responses from other nodes

---

## Implementation Notes

### DHT Security

norn implements several security measures:

1. **Per-IP Rate Limiting:** `DHTSTORE_PER_IP` limits items per source IP
2. **Byte Budget:** `dhtstore_init(budget_mb)` limits memory usage
3. **TTL Expiry:** Items expire after 2 hours (`DHTSTORE_TTL`)
4. **LRU Eviction:** Oldest items evicted when budget exceeded
5. **Signature Verification:** All mutable items verified before storage
6. **Sequence Monotonicity:** Stale sequence numbers rejected

### Bencode Format

norn uses standard bencode for DHT messages:

```
Integer:  i<value>e           → i42e
String:    <len>:<value>      → 4:spam
List:      l<elements>e       → l4:spam4:eggse
Dictionary d<key-value pairs>e → d3:foo3:bar4:spam4:eggse
```

### Node ID Generation

```c
// From Ed25519 public key
int crypto_generate_node_id(unsigned char *node_id, uint32_t ip, uint8_t seed, unsigned char *pubkey) {
    // node_id = SHA256(public_key)[:20]
    // Includes IP and seed for uniqueness
}
```

### Kademlia Routing Table

```c
#define K_BUCKET_SIZE 8
#define MAX_BUCKETS 256

typedef struct {
    kad_node_t nodes[K_BUCKET_SIZE];
    int count;
    time_t last_update;
} k_bucket_t;

typedef struct {
    unsigned char self_id[32];
    k_bucket_t buckets[MAX_BUCKETS];
    int bucket_count;
} kad_state_t;
```

### Token Security

DHT `announce_peer` requires a token from the responding node:

```c
// Token = HMAC(secret, IP + info_hash)
// Prevents spoofed announcements
```

---

## References

- [BEP-5: DHT Protocol](http://www.bittorrent.org/beps/bep_0005.html)
- [BEP-44: Mutable and Immutable Items](http://www.bittorrent.org/beps/bep_0044.html)
- [BEP-43: Read-Only DHT Nodes](http://www.bittorrent.org/beps/bep_0043.html)
- [Kademlia Paper](https://pdos.csail.mit.edu/~petar/papers/maymounkov-kademlia-lncs.pdf)
- [Ed25519 Signature Algorithm](https://ed25519.cr.yp.to/)