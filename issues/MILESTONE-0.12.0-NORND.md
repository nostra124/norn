# MILESTONE 0.12.0 — nornd DAEMON + norn IPC CLI

**Goal:** Ship the reference **norn node daemon (`nornd`)** and turn the `norn`
CLI into a thin IPC client for it. nornd runs a norn node on top of libnorn,
hosts the **cluster key-value store** (MILESTONE-0.11.0), and exposes it over a
local Unix-domain socket; `norn` connects to that socket to drive KV operations.

See [`docs/nornd.md`](../docs/nornd.md) for the full design.

## Layering (important)

**nornd is an application on top of libnorn, not a library module.** Consistent
with MILESTONE-0.3.0, libnorn surfaces only the verified peer pubkey and opaque
payloads; identity *sources* (SSH key / ssh-agent), the IPC protocol, and the
key directory are application policy. libnorn gains **nothing** for this
milestone — all code lives in `src/nornd/` and `src/norn.c` (the CLI), consuming
the existing public API (`norn_cluster_*`, the crypto-suite `sign` hook, the
DHT/overlay client).

## Settled design decisions

1. **IPC = length-prefixed bencode** over a Unix socket (reuses `bencode.c`;
   binary-safe). Request `{op, …args}`, response `{ok, val|err, …}`; `watch`
   streams event frames.
2. **Identity = the user's Ed25519 SSH key.** Node pubkey *is* the SSH pubkey.
   Supported via a key file (seed → keypair) **and ssh-agent** — which works
   directly because norn authenticates by *signing* the ephemeral DH keys (a
   crypto-suite `sign` hook), never doing static ECDH, so the raw key never
   leaves the agent and no attestation layer is needed.
3. **Namespaced CLI verbs**: `norn cluster {put,get,del,cas,watch,members,
   leader,status}` (via nornd), `norn bep44 {get,set}` (direct DHT, unchanged),
   `norn keys <id>` / `authorized-keys` (key directory), local `keygen` /
   `version`.
4. **Cluster as a fleet key directory.** Each node publishes its SSH + GPG
   public keys into the KV store (`peer/<id>/ssh`, `peer/<id>/gpg`); GPG keys
   are opaque payloads. Turns the cluster into a distributed `authorized_keys`
   + keyring.
5. **Defaults:** socket `$XDG_RUNTIME_DIR/nornd.sock` → `~/.norn/nornd.sock`;
   `put` acks on accepted/forwarded (commit-wait is a later add); single-node
   cluster by default, peers via config/flags.

## Features

| id | title | status |
|----|-------|--------|
| FEAT-027 | IPC protocol codec — length-prefixed bencode request/response | [ ] planned |
| FEAT-028 | SSH-key identity — OpenSSH ed25519 key file + ssh-agent signer | [ ] planned |
| FEAT-029 | `nornd` daemon — node + cluster host + unix-socket IPC server | [ ] planned |
| FEAT-030 | `norn` CLI refactor — thin IPC client, namespaced verbs | [ ] planned |
| FEAT-031 | Fleet key directory — publish/resolve SSH + GPG pubkeys | [ ] planned |
| FEAT-033 | Node-served KV — direct, streamed, pubkey-addressed content (`node`/`peer`) | [ ] planned |
| FEAT-032 | Packaging — nornd as a user + system daemon (systemd + launchd, socket-activated) | [ ] planned |

## Suggested order

FEAT-027 (IPC codec) and FEAT-028 (identity) are the pure, unit-testable cores.
FEAT-029 (daemon) wires them onto a libnorn client + `norn_cluster` and serves
the socket. FEAT-030 retargets the CLI at the socket. FEAT-031 layers the key
directory over the KV API.

## Acceptance (milestone-level)

1. `nornd` starts using the user's SSH key as identity (file or ssh-agent) and
   serves a Unix socket.
2. `norn cluster put k v` then `norn cluster get k` round-trips through nornd to
   the replicated KV store; `norn cluster members/leader/status` report cluster
   state; `norn cluster watch` streams changes.
3. Each node's SSH + GPG public keys are published to the cluster;
   `norn keys <id>` resolves a peer's keys and `norn authorized-keys` assembles
   an `authorized_keys` file from the cluster.
4. `norn bep44 get/set` keep today's direct-DHT semantics; `keygen`/`version`
   work without a running daemon.

## Consumer-side counterparts

The fleet (regin/dvalin/raven, wyrd packs) gets a turnkey node daemon + CLI:
pubkey-addressed shared config/state and a distributed SSH/GPG key directory,
keyed off the SSH identity it already trusts.
