---
id: FEAT-028
type: feature
priority: medium
complexity: M
estimate_tokens: 60k-120k
estimate_time: 90-180min
phase: planned
status: open
depends_on: [FEAT-013]
milestone: MILESTONE-0.12.0
spawned_from: ~
---
# SSH-key identity — OpenSSH ed25519 key file + ssh-agent signer

## Description

**As a** fleet operator
**I want** `nornd` to use my existing **Ed25519 SSH key** as its node identity
**So that** the fleet's existing SSH trust (`authorized_keys`) is the basis for
norn identity, with no new keys to manage.

The node's norn public key *is* the SSH ed25519 public key.

## Implementation

- `src/nornd/identity.{c,h}` (nornd-owned, not libnorn):
  - **Key file**: parse the OpenSSH private-key format (`-----BEGIN OPENSSH
    PRIVATE KEY-----`) for an ed25519 key → 32-byte seed →
    `crypto_sign_seed_keypair`. Support unencrypted and passphrase-decrypted
    (bcrypt-pbkdf) keys. Clear errors on RSA/ECDSA/unsupported.
  - **ssh-agent**: connect to `$SSH_AUTH_SOCK`, find the ed25519 key, and expose
    a `sign(msg) → sig` that delegates to the agent. Install it as the crypto
    suite's `sign` hook so the handshake authenticates via the agent — works
    because norn signs the ephemeral DH keys and never does static ECDH, so the
    raw key never leaves the agent and no attestation layer is needed.
  - Read the SSH **public** key (`~/.ssh/id_ed25519.pub`) for the advertised
    identity / node id.
- The OpenSSH key parser and the agent wire framing are pure and unit-testable
  with fixed vectors (a known test key, a captured agent exchange).

## Acceptance Criteria

1. Loads a real unencrypted `~/.ssh/id_ed25519` and produces a keypair whose
   public key matches the corresponding `.pub`.
2. ssh-agent mode signs a handshake transcript via the agent (verified against
   the SSH public key) without ever reading the private key.
3. Parser/agent-framing unit-tested to 100% line + branch coverage; clear errors
   on unsupported key types.

## Cross-repo

Lets the whole fleet (regin/dvalin/raven, wyrd) key node identity off the SSH
keys it already provisions.
