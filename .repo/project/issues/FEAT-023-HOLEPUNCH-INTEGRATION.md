# FEAT-023 Hole Punch Connection Integration

## Status: INCOMPLETE (wire protocol done, integration missing)

## Description

Wire up the hole punch implementation to the connection ladder. The rendezvous service and wire protocol are complete, but the actual hole punch connection flow is not integrated.

## What's Complete

- ✅ Rendezvous service (`norn_rendezvous.h/c`)
- ✅ Wire protocol (`norn_nat.h/c` - HolePunchRequest/Response)
- ✅ Binary message routing (0x10-0x1F in `norn_impl.c`)
- ✅ Probe sending (`norn_send_probes()`)
- ✅ Endpoint capability flag (`NORN_EP_CAP_RENDEZVOUS`)

## What's Missing

### 1. Connection Ladder Integration

```c
// src/libnorn/norn_session.c:127
if (endpoint->caps & NORN_EP_CAP_RENDEZVOUS) {
    // CURRENT: Sets DIAL_HOLEPUNCH but doesn't call anything
    ctx->state = DIAL_HOLEPUNCH;
    
    // NEEDED:
    // 1. Generate ephemeral key for this session
    // 2. Call norn_send_holepunch_req_async()
    // 3. Set up callback for hole punch response
    // 4. Send probes on response
    // 5. Establish session after probes
}
```

### 2. Hole Punch Request Sending

```c
// src/libnorn/norn_rendezvous.c:125
int norn_send_holepunch_req_async(norn_client_t *client,
                                   const uint8_t *target_pubkey,
                                   const uint8_t *rendezvous_pubkey,
                                   const uint8_t *my_ephemeral,
                                   norn_holepunch_callback_t callback,
                                   void *user_data) {
    // CURRENT: Encodes request but doesn't send it
    // Returns -1 (stub)
    
    // NEEDED:
    // 1. Get external IP from net_get_external_endpoint()
    // 2. Sign request with identity key
    // 3. Send via UDP to rendezvous peer
    // 4. Store callback for response handling
    // 5. Handle timeout (no response in 5 seconds)
}
```

### 3. Hole Punch Response Handling

```c
// src/libnorn/norn_impl.c:320 (in dispatch_response)
} else if (data[0] == NORN_MSG_HOLEPUNCH_RESP && len >= NORN_HOLEPUNCH_RESP_LEN) {
    if (norn_decode_holepunch_resp(&resp, data, len) == 0) {
        // CURRENT: Just a TODO comment
        // (void)resp;
        
        // NEEDED:
        // 1. Look up pending hole punch by session_id
        // 2. Verify signature from rendezvous
        // 3. Call callback with peer IP/port
        // 4. Initiate probe sending
        // 5. Transition to session establishment
    }
}
```

### 4. Probe Coordination

```c
// After receiving HolePunchResponse:
// 1. Send norn_send_probes(client, peer_ip, peer_port, 3, 100);
// 2. Wait for incoming probes
// 3. Detect successful hole punch (receive packet from peer)
// 4. Start session handshake
```

## Implementation Steps

1. **Generate Ephemeral Key**
   ```c
   unsigned char ephemeral_pub[32], ephemeral_sec[32];
   crypto_box_keypair(ephemeral_pub, ephemeral_sec);
   ```

2. **Send Hole Punch Request**
   ```c
   norn_send_holepunch_req_async(client, target, rendezvous, ephemeral_pub, callback, ctx);
   ```

3. **Handle Response**
   - Decode response
   - Verify signature
   - Get peer's external IP/port
   - Get peer's ephemeral key

4. **Send Simultaneous Probes**
   ```c
   norn_send_probes(client, peer_ip, peer_port, 3, 100);
   ```

5. **Establish Session**
   - Detect incoming probes
   - Perform session handshake using ephemeral keys
   - Transition to `NORN_SESSION_ESTABLISHED`

## Wire Protocol (Complete)

```
HolePunchRequest (0x10):
  msg_type: 1 byte
  target_pubkey: 32 bytes
  my_ephemeral_pubkey: 32 bytes
  my_external_ip: 4 bytes
  my_external_port: 2 bytes
  signature: 64 bytes
  Total: 135 bytes

HolePunchResponse (0x11):
  msg_type: 1 byte
  peer_pubkey: 32 bytes
  peer_external_ip: 4 bytes
  peer_external_port: 2 bytes
  peer_ephemeral_pubkey: 32 bytes
  signature: 64 bytes
  Total: 135 bytes
```

## Priority: High

Hole punch is essential for NAT traversal behind cone/restricted-cone NATs.

## Estimated Effort: 2-3 days

## Related: FEAT-017 (NAT Traversal)