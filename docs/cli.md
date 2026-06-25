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

### Fleet key directory

```
norn keys <nodeid>          Print a peer's SSH + GPG public keys
norn authorized-keys        Emit an authorized_keys file assembled from the cluster
norn gpg-import [<nodeid>]  Import peer GPG key(s) into the local keyring (all if omitted)
```

### Direct DHT (BEP-44, unchanged — no daemon needed for these)

```
norn bep44 get <key>            Retrieve a mutable signed record from the DHT
norn bep44 set <key> <value>    Store a mutable signed record
```

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
