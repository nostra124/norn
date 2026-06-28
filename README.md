# norn

Named after the Norns of Nordic mythology who weave destiny, **norn** is a C99
stack for peer-to-peer applications: a Mainline DHT client, secure
pubkey-addressed sessions, and a Raft-replicated cluster key-value store —
plus a node daemon and a CLI on top.

## Components

- **libnorn** — the library: Mainline DHT (BEP-5/BEP-44), authenticated
  sessions and reliable streams, NAT traversal, a private-overlay bootstrap,
  and a class-aware Raft cluster KV store. In-memory and dependency-light
  (only libsodium).
- **nornd** — the node daemon: runs a norn node and hosts the cluster KV store,
  serving local tools over a Unix socket. Identity is the user's (or host's)
  **Ed25519 SSH key**.
- **norn** — the CLI: direct DHT operations, plus a thin IPC client to `nornd`
  for the cluster store and the fleet key directory.

## Installation

### Quick Install (all platforms)

```sh
curl -fsSL https://raw.githubusercontent.com/nostra124/norn/master/scripts/install.sh | sh
```

Installs to `/usr/local` by default. Options:

```sh
PREFIX=/usr VERSION=v0.12.0 curl -fsSL ... | sh
```

### macOS (Homebrew)

```sh
brew tap nostra124/norn
brew install norn
```

### From Source

```sh
autoreconf -fi
./configure
make
make check          # unit tests (100% line+branch on tracked modules)
sudo make install   # installs libnorn, norn, nornd, man pages, service units
```

Requires a C compiler, autotools, and **libsodium** (`libsodium-dev`).

### Linux (Debian packages)

Debian packages are available in the `debian/` directory:

```sh
dpkg-buildpackage -us -uc
sudo dpkg -i ../libnorn_*.deb ../norn_*.deb
```

## Usage

Direct DHT (no daemon):

```sh
norn keygen                       # create ~/.norn/key.pem, print the pubkey
norn set <key> <value>            # publish a signed BEP-44 record
norn get <author-pubkey>          # fetch a record
norn daemon                       # run a standalone public-DHT node
```

Cluster KV and key directory (via a running `nornd`):

```sh
nornd --identity ~/.ssh/id_ed25519 --class workstation &
norn cluster put greet hello
norn cluster get greet            # -> hello
norn cluster status               # role / leader / member count
norn keys <nodeid>                # a peer's SSH + GPG public keys
```

`nornd` is socket-activated as a user or system service — see
`contrib/systemd/` and `contrib/launchd/`, e.g.
`systemctl --user enable --now nornd.socket`.

## Three key-value surfaces

| Surface        | Backed by            | For                                   |
| -------------- | -------------------- | ------------------------------------- |
| Cluster KV     | Raft replication     | small shared state; the key directory |
| Node-served KV | direct norn streams  | a node's own values, streamed, large  |
| BEP-44         | the public DHT       | small public records                  |

Only **server** nodes with proven uptime are eligible to lead the cluster, so a
mostly-offline edge fleet (laptops, phones) never stalls writes.

## Documentation

- **Man pages** (installed by `make install`): `norn(1)`, `nornd(8)`,
  `libnorn(3)`.
- **API reference** — generated from the header comments with Doxygen:

  ```sh
  make apidocs        # writes HTML + man3 into docs/api/
  ```

  (needs `doxygen`; `graphviz` is optional and off by default.)
- **Design docs** under `docs/`: `architecture.md`, `nornd.md`, `cli.md`,
  `cluster-kv.md`, `private-overlay.md`, NAT traversal, and the BEP references.

## Architecture

```
application        norn (CLI)            nornd (daemon)
                       │  IPC (unix socket)  │
                       └─────────┬───────────┘
                                 │
   libnorn ──────────────────────┴───────────────────────────────
     DHT/BEP-44 · sessions+streams · NAT/rendezvous/relay
     private overlay · stream forward · cluster KV (Raft)
                                 │
                            libsodium
```

## Integration

libnorn is the foundation layer for **bifrost** (P2P connectivity) and other
applications needing DHT discovery, secure pubkey-addressed transport, or a
fleet-wide replicated store. Rust bindings live under `bindings/rust/`.

## License

MIT. See `LICENSE`.
