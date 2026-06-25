# nornd + norn — command-line interface

Two binaries:

- **`nornd`** — the node daemon (runs the norn node, hosts the cluster KV store,
  serves the IPC socket). You mostly run it via systemd/launchd (see
  [packaging](#packaging-user--system-daemon)).
- **`norn`** — the thin CLI client that talks to `nornd` over the IPC socket.

---

## `nornd` — the daemon

```
nornd [OPTIONS]

Run a norn node: join the network, host the cluster KV store, and serve a local
IPC socket.

Identity (the node's pubkey IS its Ed25519 SSH key):
  --identity <src>     'agent'  → sign via ssh-agent ($SSH_AUTH_SOCK)
                       <path>   → OpenSSH ed25519 private key file
                       (default: agent if SSH_AUTH_SOCK set, else
                        ~/.ssh/id_ed25519 (user) / /etc/ssh/ssh_host_ed25519_key
                        (system))
  --passphrase-fd <n>  Read a key passphrase from fd n (for encrypted key files)

IPC:
  --socket <path>      IPC socket to serve (default: $XDG_RUNTIME_DIR/nornd.sock,
                       or /run/nornd/nornd.sock for the system service).
                       Honors systemd/launchd socket activation when launched
                       with a passed-in listening fd.

Network / cluster:
  --listen <ip:port>   UDP address for norn (default: ephemeral)
  --bootstrap <peer>   Public-DHT bootstrap peer (host:port); repeatable
  --private-overlay    Form a private overlay (no public mainline announce)
  --overlay-peer <pubkey@host:port>   Private-overlay bootstrap peer; repeatable
  --class <c>          Node class: server|workstation|laptop|mobile
                       (default: server for the system service, else autodetect)
  --join <pubkey@host:port>   Join an existing cluster via this server
  --no-publish-keys    Don't publish local SSH/GPG pubkeys to the key directory

Process:
  --config <path>      Config file (default: ~/.config/norn/nornd.toml or
                       /etc/norn/nornd.toml). Flags override the file.
  --data-dir <path>    Persistent state (snapshots)
  -f, --foreground     Don't daemonize (default under systemd/launchd; Type=notify)
  --log-level <lvl>    debug|info|warn|error
  --version | --help
```

`nornd` runs one event loop (`poll` over the norn UDP fd + the IPC socket +
session fds), draining `norn_tick` + `norn_cluster_tick`.

---

## Three key-value surfaces

norn exposes three distinct stores; pick by who owns the data and how consistent
it must be:

| Surface | CLI | Transport | Consistency | For |
|---------|-----|-----------|-------------|-----|
| **Cluster KV** | `norn cluster …` | Raft over norn streams | replicated, consensus | shared fleet state — **incl. the SSH/GPG key directory** |
| **Node-served KV** | `norn node …` (own), `norn peer get/cat` | direct dial → stream req/resp | per-node authoritative (no replication) | a node's own data, served on demand |
| **DHT (BEP-44)** | `norn bep44 …` | public mainline DHT | public, eventually-consistent | small public records |

The fleet's **SSH + GPG public keys are shared in the cluster store**, where
membership is controlled — **not** over the public BEP-44 DHT.

## `norn` — the client

```
norn [GLOBAL] <command> [args]

Global:
  --socket <path>   nornd socket (default: $NORN_SOCK, else autodetect
                    $XDG_RUNTIME_DIR/nornd.sock → /run/nornd/nornd.sock)
  --json            machine-readable output
  -h, --help | --version
```

### Cluster KV (routed through nornd)

```
norn cluster put <key> <value|->     Set a key (value '-' reads stdin; binary-safe)
norn cluster get <key>               Print the value (raw bytes on stdout)
norn cluster del <key>               Delete a key
norn cluster cas <key> <expect|-> <value|->   Compare-and-set
norn cluster ls [<prefix>]           List keys (optionally under a prefix)
norn cluster watch <prefix>          Stream changes under a prefix (PUT/DEL lines)
norn cluster members                 List members: pubkey, class, role (voter/learner), state
norn cluster leader                  Print the current leader's pubkey
norn cluster status                  Node + cluster status (term, leader, commit, members)
norn cluster join   <pubkey@host:port>   Join/bootstrap a cluster      (admin)
norn cluster promote <pubkey>            Learner → voter               (admin)
norn cluster remove  <pubkey>            Remove a member               (admin)
```

### Node-served KV (this node's own store)

Each node serves its own key-values **directly** on request — no replication, no
DHT. Values are **streamed** over a norn stream, so they can be large (a node is
a pubkey-addressed content server: `node put` a 1 GB file and peers stream it).

```
norn node set  <key> <value|->     Set a mutable served value (updatable)
norn node put  <value|-|@file>     Store an immutable served value → prints its content hash
norn node list [<prefix>]          List keys this node serves
norn node del  <key>               Remove a served key
```

### Peer fetch (another node's served store)

```
norn peer get  <nodeid> <key>      Fetch a peer's mutable served value (streams to stdout)
norn peer cat  <nodeid> <hash>     Fetch a peer's immutable served value by content hash
```

nornd dials `<nodeid>`, opens a stream, sends the request, and streams the reply
back — so large objects never buffer in full. Typical pattern: publish a blob
with `node put` (get its hash), advertise `hash → nodeid` as a small value in
the **cluster** store, then peers `peer cat <nodeid> <hash>` to stream it.

### Fleet key directory

```
norn keys <nodeid>          Print a peer's SSH + GPG public keys
norn authorized-keys        Emit an authorized_keys file assembled from the cluster
norn gpg-import [<nodeid>]  Import peer GPG key(s) into the local keyring (all if omitted)
```

### Direct DHT (BEP-44 — no daemon needed for these)

[BEP-44](https://www.bittorrent.org/beps/bep_0044.html) defines two DHT item
kinds; the CLI exposes both:

```
# Mutable — signed records, keyed by SHA1(pubkey[+salt]), updatable via seq:
norn bep44 set <value|->          Publish/update OUR mutable record (we sign; auto seq)
norn bep44 get <author-pubkey>    Fetch a mutable record by its author's public key
  [--salt <s>] [--seq <n>]        Optional BEP-44 salt / explicit sequence number

# Immutable — content-addressed, key = SHA1(bencode(value)), no signature:
norn bep44 put <value|->          Store an immutable item; prints its 40-hex content hash
norn bep44 cat <hash>             Fetch an immutable item by its content hash
```

`put`/`cat` are a tiny content-addressed store (the value's hash *is* the key);
`set`/`get` are signed, updatable records addressed by author pubkey.

### Local / node

```
norn id           Print this node's identity (pubkey, SSH fingerprint, class)
norn status       Alias for `norn cluster status`
norn keygen       Generate an ed25519 keypair (local; no daemon)
norn version      Print version
```

### Conventions

- `get` prints the raw value to stdout (script-friendly); `--json` wraps results
  in JSON with base64 values.
- A `value` / `expect` of `-` reads from stdin (binary-safe, large values).
- Exit codes: `0` ok · `1` error · `2` daemon unreachable · `3` key/peer not found.
- `NORN_SOCK` overrides socket discovery.

---

## Size limits

| Surface | Limit | Nature |
|---------|-------|--------|
| BEP-44 mutable value | 1000 B | spec-mandated (hard; `>1000` rejected) |
| BEP-44 immutable value | 1000 B | spec-mandated (hard) |
| Cluster KV key | 64 B | design (tunable) |
| Cluster KV value | 256 B → raise for keys | design (tunable; must fit a raft entry) |
| Raft log entry | 512 B | must be ≥ a full KV command |
| **Node-served KV value** | **effectively unbounded (streamed from disk)** | a node is a pubkey-addressed content server |
| IPC frame | ~1 MB cap | design (bounds the daemon) |
| norn stream | effectively unbounded | reliable stream — the transport bulk rides |

**Rule of thumb:** ≤1 KB consensus records → cluster KV; small public records →
BEP-44; **keys → cluster KV** (chunked if large); **files/blobs → node-served
KV** (`node put` / `peer cat`, streamed), with only a small `hash → nodeid`
pointer kept in the cluster store.

**GPG keys.** An armored GPG public key (~1–4 KB) exceeds every inline limit
above. The key directory therefore (a) raises the cluster value cap modestly to
cover the common ed25519-GPG case, and (b) **chunks** larger keys across
`peer/<id>/gpg/<n>` with a `peer/<id>/gpg` manifest (chunk count + content hash)
written last; readers gate on the manifest and verify the reassembled key. This
keeps raft/KV entries bounded (important for mobile learners) rather than
bloating every fixed-size entry. SSH ed25519 pubkeys (~80 B) fit inline.

## Packaging (user & system daemon)

`nornd` installs as **both** a per-user daemon and a system daemon, on Linux
(systemd) and macOS (launchd), with **socket activation** so the daemon starts
on the first `norn` connection.

### Identity per mode (still "your SSH key")

| Mode | Identity (default) | Socket |
|------|--------------------|--------|
| **User** (`systemctl --user`, launchd agent) | ssh-agent, else `~/.ssh/id_ed25519` | `$XDG_RUNTIME_DIR/nornd.sock` |
| **System** (`systemctl`, launchd daemon) | the host SSH key `/etc/ssh/ssh_host_ed25519_key` | `/run/nornd/nornd.sock` |

A server's node identity = its **SSH host key** (already trusted via
`known_hosts`); a user's node identity = their **SSH user key**.

### Linux — systemd (socket-activated)

- System: `nornd.socket` (`ListenStream=/run/nornd/nornd.sock`) + `nornd.service`
  (`Type=notify`, runs as the `norn` system user, `StateDirectory=nornd`).
  `sudo systemctl enable --now nornd.socket`
- User: `~/.config/systemd/user/` units; socket `%t/nornd.sock`.
  `systemctl --user enable --now nornd.socket`

Shipped under `contrib/systemd/` (system) and `contrib/systemd/user/`.

### macOS — launchd

- System daemon: `/Library/LaunchDaemons/io.norn.nornd.plist`
- User agent: `~/Library/LaunchAgents/io.norn.nornd.plist`

Both use launchd `Sockets` for socket activation. Shipped under
`contrib/launchd/`. Homebrew wires the user agent via `service norn start`.

`make install` installs the units/plists; the Homebrew formula and Debian/RPM
packaging reference them.
