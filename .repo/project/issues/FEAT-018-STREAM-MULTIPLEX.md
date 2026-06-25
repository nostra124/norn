# FEAT-018 Stream Multiplexing

## Status: IN PROGRESS

## Description

Implement stream multiplexing API to allow multiple logical streams over a single session. This enables applications to open multiple independent streams (like SSH channels) without establishing multiple sessions.

## Background

The streammux implementation (`streammux.h/c`) already exists with:
- Reliable, ordered byte streams
- Flow control
- Backpressure
- Multiple streams per session

What's missing:
- API to create/use streams from norn_session_t
- Stream lifecycle management
- Integration with session I/O

## Requirements

### 1. Stream Creation API

```c
typedef struct norn_stream norn_stream_t;

typedef enum {
    NORN_STREAM_READY,      // Stream is ready for I/O
    NORN_STREAM_CLOSED,     // Stream closed locally
    NORN_STREAM_RESET,      // Stream reset by peer
} norn_stream_state_t;

typedef void (*norn_stream_callback_t)(norn_stream_t *stream,
                                       norn_stream_state_t state,
                                       void *user_data);

int norn_stream_open_async(norn_session_t *session,
                           norn_stream_callback_t callback,
                           void *user_data);
```

### 2. Stream I/O API

```c
int norn_stream_write(norn_stream_t *stream,
                      const unsigned char *data,
                      size_t len);

int norn_stream_read(norn_stream_t *stream,
                     unsigned char *buf,
                     size_t cap);

size_t norn_stream_readable(const norn_stream_t *stream);

int norn_stream_close(norn_stream_t *stream);

int norn_stream_shutdown(norn_stream_t *stream);
```

### 3. Stream Lifecycle

- Streams are created by `norn_stream_open_async()`
- Each stream has a unique 16-bit ID
- Streams are full-duplex
- Streams can be closed independently
- Session tracks all open streams
- Streams are freed when session closes

## Implementation Plan

### Phase 1: Basic API (1-2 days)
1. Define stream handle structure
2. Implement `norn_stream_open_async()`
3. Implement basic read/write
4. Add stream tracking to session

### Phase 2: Integration (1-2 days)
1. Wire streammux to session I/O
2. Handle incoming stream data
3. Implement flow control
4. Add stream events to session tick

### Phase 3: Testing (1 day)
1. Unit tests for stream API
2. Integration test with session
3. Multi-stream test
4. Flow control test

### Phase 4: norn-forward Utility (Optional, 2-3 days)
1. TCP forwarder over norn stream
2. Unix socket forwarder
3. Local/remote forwarding modes

## Technical Details

### Stream ID Assignment

- Initiator uses odd IDs: 1, 3, 5, ...
- Responder uses even IDs: 0, 2, 4, ...
- This prevents collisions without coordination

### Flow Control

- Each stream has independent flow control
- Backpressure propagates to sender
- Window size configurable per stream

### Wire Format

Already implemented in streammux:
```
Segment Header (8 bytes):
  stream_id: 2 bytes
  flags: 2 bytes
  length: 4 bytes
  data: variable
```

## Dependencies

- FEAT-016 (Session API) ✅
- streammux implementation ✅

## Priority: Medium

## Estimated Effort: 4-5 days

## Consumers

- bifrost: multi-channel over single session
- wyrd: pack/clan channels
- norn-forward: tunnel utility