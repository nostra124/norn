# FEAT-023 Hole Punch Connection Integration

## Status: COMPLETE ✅

## Description

Wire up the hole punch implementation to the connection ladder. The rendezvous service and wire protocol are complete, and the full hole punch connection flow is now integrated.

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
- ✅ Session creation helper (`norn_session_from_probe()`)
- ✅ Session establishment on probe detection
- ✅ Dial callback notification with NORN_SESSION_ESTABLISHED

## Implementation Summary

### Complete Flow

1. **Connection Ladder** (`norn_session.c:171-194`)
   - DHT resolves peer endpoint
   - If endpoint has `NORN_EP_CAP_RENDEZVOUS`, generate ephemeral keypair
   - Send HolePunchRequest via rendezvous peer

2. **Hole Punch Response** (`norn_session.c:100-135`)
   - Receive HolePunchResponse from rendezvous
   - Extract peer's external IP/port and ephemeral pubkey
   - Send simultaneous probes to peer

3. **Probe Detection** (`norn_impl.c:347-376`)
   - Receive Probe message matching pending request
   - Call `norn_session_from_probe()` helper
   - Create session with peer endpoint
   - Set ephemeral keys for encryption
   - Notify dial callback with `NORN_SESSION_ESTABLISHED`

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

## Priority: High ✅ COMPLETE

Hole punch is essential for NAT traversal behind cone/restricted-cone NATs.

## Related: FEAT-017 (NAT Traversal)