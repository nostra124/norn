# FEAT-016 Async & Mobile-Ready Session API

**Status:** Phase 1 COMPLETE (async foundation)

## Completed

### Phase 1: Async Foundation
- [x] Refactored session API to be fully async (no blocking I/O)
- [x] Added event loop integration via `norn_tick()` and `norn_get_fd()`
- [x] Added session registration with `norn_client_add_session()`
- [x] Added session FD tracking for poll()/select() integration
- [x] Non-blocking UDP socket creation with `fcntl(O_NONBLOCK)`
- [x] Callback-based session lifecycle:
  - `norn_dial_async()` - Dial by pubkey
  - `norn_dial_direct_async()` - Dial by IP:port (testing)
  - `norn_listen_async()` - Listen for inbound connections
  - `norn_session_close_async()` - Graceful close
- [x] Mobile-ready design:
  - No blocking operations
  - Works with external event loops (libuv, CFRunLoop, epoll)
  - Battery-efficient (no busy-waiting)
- [x] Internal header `norn_internal.h` for struct visibility
- [x] Session tracking in `norn_client_t`
- [x] All tests passing (30/30)

## Architecture

### Session Flow (Async)
```
1. norn_dial_async(client, peer_pubkey, suite, on_state_change, user_data)
   - Returns immediately (non-blocking)
   - Registers session with client

2. norn_tick(client) called in event loop
   - Processes DHT packets
   - Processes session packets
   - Invokes state change callbacks

3. on_state_change(session, NORN_SESSION_ESTABLISHED, user_data)
   - Session is ready for streams
   - Open streams: norn_stream_open_async()
```

### Event Loop Integration
```c
// External event loop (libuv, epoll, etc.)
int fd = norn_get_fd(client);

while (running) {
    // Wait for activity
    poll(&pfd, 1, timeout);
    
    // Process all pending events
    norn_tick(client);
}

// Or with session FDs:
int fds[32];
int events[32];
int count = norn_get_session_fds(client, fds, events, 32);
// Use with poll()/epoll_wait()
```

### Mobile Platforms

**iOS (CFRunLoop):**
```c
int fd = norn_get_fd(client);
CFSocketContext ctx = {0, client, NULL, NULL, NULL};
CFSocketRef sock = CFSocketCreateWithNative(NULL, fd, 
    kCFSocketReadCallBack, socket_callback, &ctx);
CFRunLoopSourceRef src = CFSocketCreateRunLoopSource(NULL, sock, 0);
CFRunLoopAddSource(CFRunLoopGetCurrent(), src, kCFRunLoopDefaultMode);
```

**Android (epoll via JNI):**
```c
int fd = norn_get_fd(client);
int ep = epoll_create1(0);
struct epoll_event ev = {.events = EPOLLIN, .data.ptr = client};
epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);

// In JNI thread:
while (running) {
    int n = epoll_wait(ep, events, MAX_EVENTS, timeout);
    for (int i = 0; i < n; i++) {
        norn_tick(events[i].data.ptr);
    }
}
```

## Breaking Changes

**Removed (blocking):**
- `norn_session_handshake_initiator()` - Use `norn_dial_async()` instead
- `norn_session_handshake_responder()` - Use `norn_listen_async()` instead

**Added (async):**
- `norn_dial_async()` - Non-blocking dial
- `norn_dial_direct_async()` - Non-blocking direct dial
- `norn_listen_async()` - Non-blocking listen
- `norn_session_close_async()` - Non-blocking close
- `norn_get_session_fds()` - Get all session FDs for poll()
- `norn_stream_open_async()` - Async stream open (stub for FEAT-018)

## Next Steps

### Phase 2: NAT Traversal (FEAT-017)
- [ ] Hole punching (UDP NAT traversal)
- [ ] Relay support (TURN-style)
- [ ] STUN-like endpoint discovery

### Phase 3: Platform Abstraction (FEAT-019)
- [ ] `norn_event_loop_ops_t` abstraction
- [ ] libuv integration
- [ ] kqueue integration (macOS/iOS)
- [ ] epoll integration (Linux/Android)
- [ ] CFRunLoop integration (iOS)

### Phase 4: Stream Multiplexing (FEAT-018)
- [ ] Implement `norn_stream_open_async()`
- [ ] Implement `norn_stream_write()` / `norn_stream_read()`
- [ ] Integrate with `streammux.c`

## Testing

All 30 tests pass:
- `test_norn_session` - Session API tests
- `test_channel` - Handshake crypto
- `test_streammux` - Stream multiplexing
- SIT tests in `tests/sit/handshake.bats`

## References

- FEAT-016-ASYNC.md - Architecture design document
- docs/architecture.md - Overall architecture
- FEAT-017 - NAT traversal
- FEAT-018 - Stream multiplexing