# nornd + norn CLI — design

A small daemon (`nornd`) that runs a norn node on top of libnorn and hosts the
cluster key-value store, and a thin `norn` CLI that talks to it over local IPC.

## Components

```
   norn (CLI)                         nornd (daemon)
  ┌──────────┐   length-prefixed     ┌──────────────────────────────────┐
  │ put/get/ │   bencode over a      │  IPC server (unix socket)         │
  │ del/watch│◄──unix socket────────►│  → norn_cluster_kv_* (FEAT-025/26)│
  │ members  │                       │  norn client + cluster (libnorn)  │
  └──────────┘                       │  event loop: norn_tick+cluster_tick│
                                      │  identity: user's SSH key          │
                                      └──────────────────────────────────┘
```

- **`nornd`** owns the node: identity, DHT/overlay bootstrap, the
  `norn_cluster` replica, and the single event loop. It serves a Unix-domain
  socket and maps each request to a `norn_cluster_kv_*` call.
- **`norn`** is a thin IPC client: connect, send one request, print the reply,
  exit. `watch` stays connected and streams events. `keygen` / `version` are
  local (no daemon).

## Identity — the user's SSH key

A nornd node is identified by the user's **Ed25519 SSH key**; the node's norn
public key *is* the SSH ed25519 public key. Two ways to hold it:

- **Key file** (`~/.ssh/id_ed25519`, unencrypted or passphrase-decrypted): parse
  the OpenSSH private-key format → 32-byte seed → libsodium Ed25519 keypair.
- **ssh-agent**: delegate signing to the agent.

> **Why ssh-agent works cleanly.** norn's handshake authenticates by *signing*
> the ephemeral X25519 keys with the static Ed25519 identity
> (`channel_auth_sign → bf_sign`); the static identity never does ECDH (that's
> ephemeral-only). `sign` is a crypto-suite vtable hook, so an agent-backed
> signer plugs in directly: the agent signs the handshake transcript, the raw
> key never leaves the agent, and **no attestation/delegation layer is needed**.
> The node's advertised pubkey is the SSH pubkey; peers verify handshakes
> against it.

This makes a fleet's existing SSH trust (`authorized_keys`) the basis for norn
identity.

## The cluster as a fleet key directory

Each node publishes its own public keys into the replicated KV store, so any
member can resolve another member's keys — a distributed `authorized_keys` +
GPG keyring:

| key | value |
|-----|-------|
| `peer/<nodeid>/ssh` | the member's SSH public key (`ssh-ed25519 AAAA…`) |
| `peer/<nodeid>/gpg` | the member's GPG public key (ASCII-armored block) |

`<nodeid>` is the member's norn/SSH pubkey (hex). On startup nornd reads the
local SSH pubkey (`~/.ssh/id_ed25519.pub`) and GPG pubkey (`gpg --export
--armor`) and `cluster_kv_put`s them; they replicate to every member. GPG keys
are opaque to norn (just KV values) — they ride the store for the app's benefit
(verifying signed commits/messages, encrypting to a peer).

CLI conveniences over this:
- `norn keys <nodeid>` — print a peer's SSH + GPG keys.
- `norn authorized-keys` — assemble an `authorized_keys` file from the cluster.

## IPC protocol — length-prefixed bencode

Each message is a 4-byte big-endian length followed by a bencoded dict (reuses
libnorn's `bencode.c`; binary-safe, no new dependency).

**Request** `{ "op": <verb>, ...args }`, e.g.
`{ "op":"put", "key":<bytes>, "val":<bytes> }`,
`{ "op":"cas", "key":<bytes>, "expect":<bytes>, "val":<bytes> }`,
`{ "op":"watch", "prefix":<bytes> }`.

**Response** `{ "ok": 0|1, "val":<bytes>?, "err":<str>?, ... }`. For `members`
the reply carries a list; for `leader` a pubkey. A `watch` connection streams
one response frame per change until the client disconnects.

The request/response **codec is pure and unit-testable** (`norn_ipc.{c,h}`); the
socket I/O and event loop are daemon glue (excluded from unit coverage, like
the rest of the network layer).

## CLI verbs (namespaced by subsystem)

```
norn cluster put   <key> <value>      # propose to the cluster KV (via nornd)
norn cluster get   <key>
norn cluster del   <key>
norn cluster cas   <key> <expect> <value>
norn cluster watch <prefix>
norn cluster members | leader | status
norn bep44   get   <key>              # direct DHT BEP-44 record (unchanged)
norn bep44   set   <key> <value>
norn keys <nodeid> | authorized-keys  # fleet key directory
norn keygen | version                 # local, no daemon
```

`cluster *` and `keys`/`authorized-keys` route through nornd; `bep44 *` keep
today's direct-DHT semantics; `keygen`/`version` stay local.

## Layering — this is nornd, not libnorn

**All of the above is `nornd` application code, not library code.** Consistent
with norn's mission (MILESTONE-0.3.0), libnorn surfaces only the verified peer
pubkey and opaque payloads; identity *sources* (SSH key / ssh-agent), the IPC
protocol, and the key directory are application policy. nornd is an app on top
of libnorn — like any downstream application — consuming the existing public API
(`norn_cluster_*`, the crypto-suite `sign` hook for agent-backed identity, the
DHT/overlay client). libnorn gains **nothing** new for this; everything lives in
nornd.

Placement: a new `src/nornd/` tree built as the `nornd` binary, with `src/norn.c`
becoming the thin CLI client. nornd's modules (IPC codec, SSH-identity loader,
key directory) are nornd's own — they may have their own tests, but they are
**not** added to libnorn's `coverage-tracked` set.

## Implementation plan (all in nornd)

1. **`src/nornd/ipc.{c,h}`** — pure bencode request/response codec. Unit-tested.
2. **`src/nornd/identity.{c,h}`** — load identity from an OpenSSH ed25519 key
   file (parse → seed → keypair); ssh-agent signer (suite `sign` delegated to
   the agent) as a second step.
3. **`src/nornd/keydir.{c,h}`** — publish/resolve SSH + GPG pubkeys over
   `norn_cluster_kv_*`.
4. **`nornd` binary** — identity + libnorn client + cluster + event loop + unix
   socket server.
5. **`norn` CLI** — refactor into the namespaced verbs; `cluster *` / `keys`
   become IPC calls to nornd; `bep44 *` keep today's direct-DHT semantics;
   `keygen` / `version` stay local.

## Open questions

- Socket path default (`$XDG_RUNTIME_DIR/nornd.sock` vs `~/.norn/nornd.sock`).
- `put` ack semantics: reply on *accepted/forwarded* vs on *committed* (would
  need a commit-wait surfaced from the cluster API).
- Single-node default vs configured cluster membership at startup.
- Does nornd live in this repo (reference daemon for libnorn, paired with the
  `norn` CLI) or as a separate downstream repo?
