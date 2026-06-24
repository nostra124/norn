# FEAT-016: norn_dial(pubkey) → session

## Overview

Connect by public key, not IP address. This lifts the connection glue from
bifrost's `session.c`/`sio.c` into norn so all consumers share one connect path.

## Dial Flow

```
norn_dial(client, pubkey)
    │
    ├─→ Resolve endpoint (DHT signed record via norn_idexch)
    │   └─→ GET target=SHA256("k" || pubkey) from DHT
    │
    ├─→ NAT Traversal
    │   ├─→ Direct (if public IP)
    │   ├─→ Hole-punch (rendezvous, BEP-55-style)
    │   └─→ Relay (onion circuit fallback)
    │
    └─→ Channel Handshake (FEAT-013 crypto suite)
        └─→ norn_session_t (verified pubkey)
            │
            ├─→ Stream (TCP-like, reliable, ordered)
            └─→ Datagram (UDP-like, unreliable)
```

## API Design

### Session Lifecycle

```c
/* Opaque handles */
typedef struct norn_session norn_session_t;
typedef struct norn_stream norn_stream_t;

/* Session callbacks */
typedef void (*norn_session_callback_t)(void *user_data, norn_session_t *session);
typedef void (*norn_stream_callback_t)(void *user_data, norn_stream_t *stream);

/* Dial out: resolve → punch → handshake → session */
norn_session_t *norn_dial(norn_client_t *client,
                           const unsigned char *pubkey,
                           const norn_crypto_suite_t *suite);

/* Listen for inbound: advertise endpoint via DHT */
int norn_listen(norn_client_t *client, uint16_t port);

/* Accept inbound session (blocking or callback) */
norn_session_t *norn_accept(norn_client_t *client);

/* Close session */
void norn_session_close(norn_session_t *session);
void norn_session_free(norn_session_t *session);

/* Get verified peer pubkey */
int norn_session_get_peer(const norn_session_t *session, unsigned char *pubkey);

/* Open a logical stream over session */
norn_stream_t *norn_stream_open(norn_session_t *session);
int norn_stream_write(norn_stream_t *stream, const void *data, size_t len);
int norn_stream_read(norn_stream_t *stream, void *buf, size_t cap);
void norn_stream_close(norn_stream_t *stream);
```

### NAT Traversal

```c
/* Endpoint record (stored in DHT) */
typedef struct {
    unsigned char pubkey[32];      /* Ed25519 or secp256k1 */
    uint32_t ip;                    /* Public IP (or 0 if behind NAT) */
    uint16_t port;                  /* Public port */
    unsigned char payload[1024];    /* App-specific (capabilities, etc.) */
    size_t payload_len;
} norn_endpoint_t;

/* Announce endpoint via DHT */
int norn_announce_endpoint(norn_client_t *client,
                            const norn_endpoint_t *ep,
                            const unsigned char *secret);

/* Resolve peer endpoint */
int norn_resolve_endpoint(norn_client_t *client,
                          const unsigned char *pubkey,
                          norn_endpoint_t *ep);
```

## Implementation Phases

### Phase 1: Core Session API (this PR)
- `norn_session_t` structure
- `norn_dial()` skeleton (direct connection only)
- `norn_listen()` / `norn_accept()`
- Integration with existing `channel.h` handshake
- Integration with existing `stream.h` / `streammux.h`

### Phase 2: NAT Traversal (FEAT-017)
- Hole-punch via rendezvous
- Relay fallback
- Endpoint record format

### Phase 3: Callbacks & Async
- Non-blocking dial with callback
- Session state machine
- Timeout handling

## Files

- `src/libnorn/norn_session.h` — Public API
- `src/libnorn/norn_session.c` — Implementation
- `tests/test_norn_session.c` — Unit tests

## Dependencies

- FEAT-013 (crypto suite) — for channel handshake
- FEAT-015 (generic idexch) — for endpoint record format
- channel.h — handshake protocol
- stream.h / streammux.h — stream multiplexing
- transport.h — UDP/TCP I/O

## Acceptance Criteria

1. Two norn clients can dial each other by pubkey over loopback
2. Initiator learns verified responder pubkey
3. Responder learns verified initiator pubkey
4. Multiple streams mux over one session
5. All tests pass (existing + new session tests)

## Testing Strategy

- Unit tests for session struct, endpoint parsing
- Integration test: loopback dial/accept
- Manual test: cross-machine direct connection
- (FEAT-017) NAT traversal tests with simulated NATs