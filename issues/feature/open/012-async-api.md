---
id: FEAT-012
type: feature
priority: critical
complexity: XL
estimate_tokens: 150k-300k
estimate_time: 180-360min
phase: open
status: done
depends_on: [FEAT-001]
milestone: MILESTONE-0.2.0
spawned_from: ~
---
# Implement Async API (Critical)

The async API in `norn.h` is declared but not implemented. This is critical for a foundational library that will be used by many applications.

## Problem

### Current State

**Header declares async API** (correct design):
```c
int norn_get_mutable(norn_client_t *client,
                     const unsigned char *pubkey,
                     norn_get_callback_t callback, void *user_data);

int norn_tick(norn_client_t *client);
int norn_get_fd(const norn_client_t *client);
```

**Implementation is stub** (broken):
```c
int norn_get_mutable(...) {
    (void)user_data;  /* TODO: implement */
    return 0;         /* Does nothing! */
}

int norn_tick(norn_client_t *client) {
    return 0;         /* Does nothing! */
}
```

**Core implementation is blocking**:
```c
// mainline_lookup: BLOCKING synchronous
int sock = socket(AF_INET, SOCK_DGRAM, 0);  // Creates own socket
select(sock + 1, &rf, NULL, NULL, &tv);      // Blocks until timeout
recvfrom(sock, buf, sizeof(buf), 0, ...);    // Blocking recv
```

### Impact

1. **Applications cannot integrate with event loops** — No `select()`/`poll()`/`epoll()` integration
2. **Blocks entire application** — Synchronous network I/O stops all processing
3. **Not usable in production** — Real applications need async I/O
4. **Thread-unsafe** — No thread safety for multi-threaded applications

## Requirements

### 1. Non-Blocking I/O

All network operations must be non-blocking:

```c
/* Send query without blocking */
sendto(client->fd, buf, len, MSG_DONTWAIT, ...);

/* Receive without blocking */
recv(client->fd, buf, len, MSG_DONTWAIT);
```

### 2. Transaction Queue

Track pending requests with callbacks:

```c
typedef struct {
    uint32_t id;
    time_t created;
    int type;                    /* GET_MUTABLE, GET_IMMUTABLE, DISCOVER */
    unsigned char target[32];
    norn_get_callback_t callback;
    void *user_data;
} norn_transaction_t;

struct norn_client {
    /* ... existing fields ... */
    norn_transaction_t transactions[MAX_TRANSACTIONS];
    int transaction_count;
};
```

### 3. Event Loop Integration

```c
/* Get socket FD for select()/poll()/epoll() */
int norn_get_fd(const norn_client_t *client);

/* Process pending packets (non-blocking) */
int norn_tick(norn_client_t *client);

/* Usage:
 * FD_SET(norn_get_fd(client), &rfds);
 * if (select(...) > 0 && FD_ISSET(norn_get_fd(client), &rfds)) {
 *     norn_tick(client);  // Invokes callbacks
 * }
 */
```

### 4. Async Operations

```c
/* Issue async get — returns immediately, callback invoked later */
int norn_get_mutable(norn_client_t *client,
                     const unsigned char *pubkey,
                     norn_get_callback_t callback, void *user_data);

/* Issue async discover — callback for each peer found */
int norn_discover(norn_client_t *client,
                  const unsigned char *info_hash,
                  norn_peer_callback_t callback, void *user_data);

/* Issue async bootstrap — returns immediately */
int norn_bootstrap(norn_client_t *client);
```

## Implementation Plan

### Phase 1: Refactor mainline.c for Non-Blocking

Currently `mainline_lookup` creates its own socket and blocks. Refactor:

**Before** (blocking):
```c
int mainline_lookup(mainline_state_t *state, ...) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);  // Own socket
    for (;;) {
        sendto(sock, ...);
        select(sock + 1, &rf, NULL, NULL, &tv);  // Block
        recvfrom(sock, ...);
    }
    close(sock);
}
```

**After** (non-blocking):
```c
int mainline_lookup_start(mainline_state_t *state, ..., callback, user_data) {
    /* Add to pending transactions */
    /* Send initial queries on main socket (non-blocking) */
    return 0;  /* Return immediately */
}

int mainline_tick(mainline_state_t *state) {
    /* Non-blocking poll on main socket */
    fd_set rf;
    struct timeval tv = {0, 0};
    FD_ZERO(&rf);
    FD_SET(state->net->fd, &rf);
    
    if (select(state->net->fd + 1, &rf, NULL, NULL, &tv) <= 0)
        return 0;
    
    /* Receive all pending packets */
    int processed = 0;
    while (1) {
        uint8_t buf[2048];
        ssize_t n = recv(state->net->fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0) break;
        
        /* Parse and dispatch to callbacks */
        mainline_process_packet(state, buf, n, from_ip, from_port);
        processed++;
    }
    return processed;
}
```

### Phase 2: Implement Transaction Queue

```c
/* norn_impl.c */

int norn_get_mutable(norn_client_t *client,
                     const unsigned char *pubkey,
                     norn_get_callback_t callback, void *user_data) {
    if (!client || !pubkey || !callback) return -1;
    
    /* Create transaction */
    norn_transaction_t *txn = transaction_new(client);
    if (!txn) return -1;
    
    txn->type = TXN_GET_MUTABLE;
    txn->callback = callback;
    txn->user_data = user_data;
    memcpy(txn->target, pubkey, 32);
    
    /* Build and send GET query */
    unsigned char buf[2048];
    int len = build_get_mutable(buf, sizeof(buf), pubkey);
    if (len < 0) {
        transaction_free(client, txn);
        return -1;
    }
    
    /* Non-blocking send */
    sendto(client->fd, buf, len, MSG_DONTWAIT, ...);
    
    return 0;
}

int norn_tick(norn_client_t *client) {
    if (!client) return -1;
    return mainline_tick(client->ml);
}
```

### Phase 3: Callback Dispatch

```c
/* mainline.c */

void mainline_process_packet(mainline_state_t *state,
                             const uint8_t *data, size_t len,
                             uint32_t from_ip, uint16_t from_port) {
    bencode_value_t *msg = bencode_decode(data, len, &pos);
    if (!msg) return;
    
    /* Check if response to pending transaction */
    bencode_value_t *tid = bencode_dict_get(msg, "t");
    uint32_t tid_val = decode_tid(tid);
    
    norn_transaction_t *txn = transaction_find(state, tid_val);
    if (!txn) {
        bencode_free(msg);
        return;
    }
    
    /* Dispatch to callback */
    switch (txn->type) {
        case TXN_GET_MUTABLE:
            if (txn->callback) {
                /* Extract value and invoke callback */
                txn->callback(txn->user_data, value, value_len);
            }
            transaction_remove(state, txn);
            break;
        /* ... other types ... */
    }
    
    bencode_free(msg);
}
```

### Phase 4: Update Tests

Add async test patterns:

```c
/* tests/test_norn_async.c */

static void on_get(void *user_data, const unsigned char *value, size_t len) {
    int *called = user_data;
    *called = 1;
    assert(value != NULL);
    assert(len > 0);
}

static void test_get_mutable_async(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg = {0};
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    int called = 0;
    int ret = norn_get_mutable(client, pk, on_get, &called);
    assert(ret == 0);
    
    /* Should return immediately without callback */
    assert(called == 0);
    
    /* Tick to process */
    norn_tick(client);
    
    /* Callback still not invoked (no network response in unit test) */
    assert(called == 0);
    
    norn_free(client);
    printf("  test_get_mutable_async: OK\n");
}

static void test_tick_null(void) {
    int ret = norn_tick(NULL);
    assert(ret == -1 || ret == 0);
    printf("  test_tick_null: OK\n");
}

static void test_get_fd(void) {
    unsigned char pk[32], sk[64];
    crypto_sign_keypair(pk, sk);
    
    norn_config_t cfg = {0};
    norn_client_t *client = norn_new(pk, sk, &cfg);
    assert(client != NULL);
    
    int fd = norn_get_fd(client);
    assert(fd >= -1);  /* -1 if no socket yet */
    
    norn_free(client);
    printf("  test_get_fd: OK\n");
}
```

## Files to Create/Modify

### New Files
- `tests/test_norn_async.c` — Async API tests

### Modified Files
- `src/libnorn/norn_impl.c` — Implement async operations
- `src/libnorn/mainline.c` — Refactor for non-blocking I/O
- `src/libnorn/mainline.h` — Add `mainline_tick`, `mainline_get_fd`
- `Makefile.am` — Add `test_norn_async` to `check_PROGRAMS`
- `tests/coverage-tracked.txt` — Add `norn_impl.c`

## Acceptance Criteria

1. ✅ `norn_get_mutable` returns immediately (non-blocking)
2. ✅ `norn_get_immutable` returns immediately (non-blocking)
3. ✅ `norn_discover` returns immediately (non-blocking)
4. ✅ `norn_bootstrap` returns immediately (non-blocking)
5. ✅ `norn_tick` processes pending packets without blocking
6. ✅ `norn_get_fd` returns valid FD for `select()`/`poll()`/`epoll()`
7. ✅ Callbacks invoked from `norn_tick` when responses arrive
8. ✅ No blocking network I/O in async code paths
9. ✅ Thread-safe: all state in `norn_client_t`, no globals
10. ✅ 100% line and branch coverage for async paths

## Migration Guide

### Before (Blocking)

```c
/* OLD: Blocks for up to 5 seconds */
int ret = norn_get_mutable(client, pubkey, on_get, NULL);
/* Application frozen until response or timeout */
```

### After (Async)

```c
/* NEW: Returns immediately */
int ret = norn_get_mutable(client, pubkey, on_get, NULL);
/* Application continues */

/* Application's event loop */
for (;;) {
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(norn_get_fd(client), &rf);
    FD_SET(stdin_fd, &rf);
    
    struct timeval tv = {0, 100000};  /* 100ms */
    select(max_fd + 1, &rf, NULL, NULL, &tv);
    
    if (FD_ISSET(norn_get_fd(client), &rf)) {
        norn_tick(client);  /* Invokes on_get callback */
    }
    
    /* Handle other I/O */
}
```

## References

- BEP-5: DHT Protocol
- BEP-44: Mutable/Immutable Items
- `libuv` — async I/O patterns
- `libevent` — event loop patterns
- `zeromq` — non-blocking socket patterns