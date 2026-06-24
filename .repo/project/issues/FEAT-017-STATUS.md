# FEAT-017 Implementation Status

## Completed (Phase 1-2)

- ✅ Endpoint discovery with async DHT
- ✅ Endpoint cache with TTL
- ✅ Direct connection integration
- ✅ NAT wire protocol (encode/decode)

## In Progress (Phase 3)

### External IP Discovery

We use DHT-based discovery instead of STUN:

1. **DHT responses** include our external IP (from peer's view)
2. **Rendezvous** tells us our IP in HolePunchResponse
3. **Application** can set IP in `norn_endpoint_t`
4. **Cache** from previous successful connections

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

### Phase 3 Completion
1. Store external IP from DHT responses
2. Implement rendezvous coordination service
3. Implement simultaneous probe sending
4. Integrate hole punch into connection ladder

### Phase 4
1. Relay circuit creation
2. Layered encryption
3. Relay forwarding

### Phase 5
1. End-to-end testing
2. Metrics and logging