# FEAT-023 Hole Punch Connection Integration

## Status: 95% COMPLETE (only session creation on probe remaining)

## Description

Wire up the hole punch implementation to the connection ladder. The rendezvous service and wire protocol are complete, but the actual hole punch connection flow is not integrated.

## What's Complete

- ✅ Rendezvous service (`norn_rendezvous.h/c`)
- ✅ Wire protocol (`norn_nat.h/c` - HolePunchRequest/Response/Probe)
- ✅ Binary message routing (0x10-0x1F in `norn_impl.c`)
- ✅ Probe sending with ephemeral pubkey (`norn_send_probes()`)
- ✅ Probe detection in dispatch_response
- ✅ Endpoint capability flag (`NORN_EP_CAP_RENDEZVOUS`)
- ✅ Connection ladder integration in `on_endpoint_resolved()`
- ✅ Ephemeral key generation in dial_context_t
- ✅ Hole punch request sending via `norn_send_holepunch_req_async()`
- ✅ Hole punch response callback handling
- ✅ Callback storage in `client->holepunch_pending[]`
- ✅ Probe message format (NORN_MSG_PROBE 0x12)

## What's Missing

### 1. Session Creation on Probe Detection

```c
// src/libnorn/norn_impl.c:347 (in dispatch_response probe detection)
if (data[0] == NORN_MSG_PROBE && len >= NORN_PROBE_LEN) {
    norn_probe_t probe;
    if (norn_decode_probe(&probe, data, len) == 0) {
        // Check if this matches a pending hole punch
        for (int i = 0; i < client->holepunch_pending_count; i++) {
            if (client->holepunch_pending[i].active &&
                memcmp(client->holepunch_pending[i].ephemeral_pubkey,
                       probe.ephemeral_pubkey, 32) == 0) {
                // NEEDED (last 5%):
                // 1. Create session with from_ip/from_port
                // 2. Use ephemeral keys for encryption
                // 3. Invoke callback with NORN_SESSION_ESTABLISHED
                // 4. Clean up pending request
                break;
            }
        }
    }
    return;
}
```

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

Probe (0x12):
  msg_type: 1 byte
  ephemeral_pubkey: 32 bytes
  Total: 33 bytes
```

## Priority: High

Hole punch is essential for NAT traversal behind cone/restricted-cone NATs.

## Estimated Effort: 1-2 days (remaining 5%)

## Related: FEAT-017 (NAT Traversal)