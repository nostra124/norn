# FEAT-017 Implementation Status

## Completed (Phase 1-3)

- ✅ Endpoint discovery with async DHT
- ✅ Endpoint cache with TTL
- ✅ Direct connection integration
- ✅ NAT wire protocol (encode/decode)
- ✅ Rendezvous coordination service
- ✅ Pending request tracking
- ✅ External IP discovery (DHT-based, no STUN)

## In Progress (Phase 3 Completion)

### External IP Discovery

We use DHT-based discovery instead of STUN:

1. **DHT responses** include our external IP (from peer's view)
2. **Rendezvous** tells us our IP in HolePunchResponse
3. **Application** can set IP in `norn_endpoint_t`
4. **Cache** from previous successful connections

### Rendezvous Coordination

**Implementation**: `norn_rendezvous.h/c`

```c
// When acting as rendezvous
norn_rendezvous_init(&rv);
norn_rendezvous_handle_req(&rv, &req, from_ip, from_port, client, &resp);

// When wanting to connect to peer
norn_send_holepunch_req_async(client, target, rendezvous, ephemeral, callback, user_data);
```

### Hole Punch Algorithm

```
Initiator                          Rendezvous                        Responder
   |                                   |                                 |
   |-------- HolePunchRequest ------->|                                 |
   |        (target=responder)         |                                 |
   |                                   |-------- HolePunchRequest ------>|
   |                                   |        (target=initiator)        |
   |                                   |                                 |
   |<------ HolePunchResponse ---------|-------- HolePunchResponse ----->|
   |        (responder's IP/port)      |        (initiator's IP/port)     |
   |                                   |                                 |
   |-------- UDP probe --------------->|<-------- UDP probe --------------|
   |        (to responder's IP)        |        (to initiator's IP)       |
   |<------- UDP probe ----------------|-------- UDP probe --------------->|
   |        (NAT mapping created)       |        (NAT mapping created)      |
   |                                   |                                 |
   |-------- Channel INIT ------------>|<-------- Channel INIT -----------|
   |        (handshake begins)          |        (handshake begins)         |
```

### Wire Protocol (Complete)

```c
// HolePunchRequest (135 bytes)
typedef struct {
    uint8_t msg_type;              // 0x10
    uint8_t target_pubkey[32];      
    uint8_t my_ephemeral_pubkey[32]; 
    uint32_t my_external_ip;        
    uint16_t my_external_port;      
    uint8_t signature[64];          
} norn_holepunch_req_t;

// HolePunchResponse (135 bytes)
typedef struct {
    uint8_t msg_type;              // 0x11
    uint8_t peer_pubkey[32];        
    uint32_t peer_external_ip;       
    uint16_t peer_external_port;    
    uint8_t peer_ephemeral_pubkey[32];
    uint8_t signature[64];          
} norn_holepunch_resp_t;
```

## Remaining Work

### Phase 3 Finalization
1. ✅ Rendezvous coordination (done)
2. ⏳ Implement `norn_send_holepunch_req_async()`
3. ⏳ Implement `norn_send_probes()` - simultaneous UDP probe sending
4. ⏳ Integrate into connection ladder

### Phase 4
1. Relay circuit creation
2. Layered encryption
3. Relay forwarding

### Phase 5
1. End-to-end testing
2. Metrics and logging