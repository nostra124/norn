# norn — Mainline DHT client library

Named after the Norns of Nordic mythology who control destiny, this library
provides a reusable mainline DHT client for peer discovery and bootstrap.

## Components

- **libnorn** — In-memory DHT client library (no config/file I/O)
- **norn** — CLI with set/get + über daemon (systemd/launchd)

## Building

```
autoreconf -fi
./configure
make
make check
sudo make install
```

## Usage

```
norn set <key> <value>      # Store a mutable signed record
norn get <key>               # Retrieve a record
norn daemon                  # Run the DHT node as a daemon
```

## Architecture

```
libnorn
├── norn.c/h      — Main client API (norn_get, norn_set, norn_bootstrap)
├── kademlia.c/h  — Kademlia protocol implementation
├── bep44.c/h     — BEP-44 mutable signed records
├── sha1.c/h      — SHA-1 for BEP-44
└── dhtstore.c/h  — In-memory DHT node storage
```

## Integration

libnorn is used by:
- **bifrost** — P2P connectivity layer for bootstrap/discovery
- **Other projects** — Any application needing mainline DHT functionality