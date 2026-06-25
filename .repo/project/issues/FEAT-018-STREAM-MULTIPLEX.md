# FEAT-018 Stream Multiplexing

## Status: IN PROGRESS (Phase 1 Complete)

## Description

Implement stream multiplexing API to allow multiple logical streams over a single session. This enables applications to open multiple independent streams (like SSH channels) without establishing multiple sessions.

## Background

The streammux implementation (`streammux.h/c`) already exists with:
- Reliable, ordered byte streams
- Flow control
- Backpressure
- Multiple streams per session

What's now implemented:
- ✅ API to create/use streams from norn_session_t
- ✅ Stream lifecycle management
- ⏳ Integration with session I/O (Phase 2)

## Progress

### Phase 1: Basic API ✅ COMPLETE
1. ✅ Define stream handle structure
2. ✅ Implement `norn_stream_open_async()`
3. ✅ Implement basic read/write
4. ✅ Add stream tracking to session

**Completed:** 2026-06-25

### Phase 2: Integration (IN PROGRESS)
1. Wire streammux to session I/O
2. Handle incoming stream data
3. Implement flow control
4. Add stream events to session tick

### Phase 3: Testing (PLANNED)
1. Unit tests for stream API
2. Integration test with session
3. Multi-stream test
4. Flow control test

### Phase 4: norn-forward Utility (Optional, FUTURE)
1. TCP forwarder over norn stream
2. Unix socket forwarder
3. Local/remote forwarding modes

## API

### Stream Creation

```c
norn_stream_t *stream = norn_stream_open_async(session, callback, user_data);
```

### Stream I/O

```c
int written = norn_stream_write(stream, data, len);
int bytes = norn_stream_read(stream, buf, cap);
size_t available = norn_stream_readable(stream);
```

### Stream Lifecycle

```c
int err = norn_stream_close(stream);  // Graceful close (FIN)
int err = norn_stream_reset(stream);  // Immediate reset (RST)
int closed = norn_stream_peer_closed(stream);  // Check peer FIN
```

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

## Estimated Effort: 4-5 days (2 days done, 2-3 remaining)

## Consumers

- bifrost: multi-channel over single session
- wyrd: pack/clan channels
- norn-forward: tunnel utility