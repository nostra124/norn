# norn — Development Methodology

## TDD (Test-Driven Development)
Every feature follows the file-first issue → failing test → implement → commit pattern:
1. Write the issue describing the feature
2. Write a failing test
3. Implement just enough to pass
4. Commit with issue reference

## Coverage
All new code must have 100% line and branch coverage. The `make coverage` target enforces this.

## Code Style
- C99 with libsodium for crypto
- No heap allocations in hot paths (use arenas/pools where needed)
- All public APIs return int (0 success, -1 error) or pointer (NULL on error)
- NULL-safe: all public functions handle NULL inputs gracefully
- Header files document invariants and ownership

## File Organization
```
src/libnorn/     — library implementation
src/nornd/       — daemon (nornd)
src/norn.c       — CLI (thin wrapper over library)
tests/unit/      — unit tests (one test_*.c per source file)
tests/sit/       — system integration tests (.bats)
tests/pit/       — performance / load tests (.bats)
contrib/         — deploy script, systemd units, launchd plists
docs/            — architecture docs, BEP references
```

## Supported Platforms
Conventions (inherited house style):
- Linux: alpine, debian, ubuntu, fedora, rhel, centos, arch, opensuse
- macOS: homebrew, macports
- FreeBSD

## Dependencies
- libsodium >= 1.0.0
- autoconf, automake, libtool (build-only)
- gcc or clang
- make

## Building
```
autoreconf -fi
./configure
make -j$(nproc)
make check
```

## Installing

**Preferred — Debian package (installs to /usr/bin, /usr/lib):**
```
dpkg-buildpackage -us -uc -b
sudo dpkg -i ../norn_*.deb ../libnorn_*.deb ../libnorn-dev_*.deb
```
Do NOT use `sudo make install` — it installs to `/usr/local` and conflicts with the deb.

**Deploying to remote hosts:**
```
contrib/deploy user@host          # probe + build + install on one host
contrib/deploy all                # deploy to all remembered hosts
NORN_DEPLOY_HOSTS=~/.config/norn/deploy-hosts contrib/deploy all
```
The deploy script (three-phase: probe/build/deploy) mirrors bifrost's `contrib/deploy`.

## Testing
```
make check           # Run unit tests (tests/unit/)
contrib/pit/run.sh   # Performance / load tests
contrib/sit/run.sh   # System integration tests
make coverage        # Generate coverage report (enforces 100%)
```

## CLI Verb Conventions

### Adding a new verb
Every new verb must be implemented end-to-end across all layers:
1. **CLI dispatch** (`src/norn.c`) — parse the verb, validate args, call IPC or library
2. **IPC handler** (`src/nornd/main.c`) — handle the IPC op in `serve_client()`
3. **Help text** — update both `usage()` and the subcommand help block (e.g. `node_help:`)
4. **Update issue file** (`issues/feature/done/`) with FEAT-NNN description

### Verb groups
```
norn node         — daemon management (local nornd)
norn peer         — remote peer interaction (over network)
norn bep44        — DHT signed records (get/set)
norn cluster      — cluster KV store (via IPC to nornd)
norn keys         — resolve peer SSH+GPG keys (via IPC)
norn version      — print version
```

### Help output rules
- **Options before Commands** in `usage()` and subcommand help
- **No environment variables** in help (use CLI arguments only)
- **No inline subcommand lists** — each group level only shows its direct children
- **Short descriptions** — one line, no wrapping/padding tricks
- **Exit codes**: no-args → exit 1, `--help` → exit 0

### Help text template for a command group
```c
/* In the dispatch, when no subcommand is given: */
if (optind + 1 >= argc) {
    fprintf(stdout, "Usage: %s node <subcommand> [ARGS...]\n", prog_name);
    fprintf(stdout, "\nSubcommands:\n");
    fprintf(stdout, "  %-15s %s\n", "start",    "Start the nornd daemon");
    fprintf(stdout, "  %-15s %s\n", "status",   "Check if nornd is running");
    fprintf(stdout, "  %-15s %s\n", "keygen",   "Generate an Ed25519 keypair");
    fprintf(stdout, "\nSee `%s --help` for top-level options.\n", prog_name);
    return 1;  /* exit 1 when user omitted subcommand */
}
```

### Status-like output format
Use recfile (key=value per line) for status/info commands:
```
pid=1234
uptime=3600
dht_nodes=42
is_leader=1
cluster_members=3
```
The IPC response should already be formatted as recfile in the `resp.val` buffer, so the CLI can just `fwrite()` it to stdout.

### IPC convention
| IPC op | Direction | Response |
|--------|-----------|----------|
| `ping` | CLI → nornd | `"pong"` (health check, no recfile) |
| `node-public` | CLI → nornd | 32-byte Ed25519 public key |
| `node-secret` | CLI → nornd | 64-byte Ed25519 secret key |
| `node-stats` | CLI → nornd | recfile: pid, uptime, dht_nodes, is_leader, cluster_members |
| `peers` | CLI → nornd | recfile: dht_nodes |

New IPC ops that return structured data should produce recfile output in `resp.val`.

### Adding a new node subcommand
1. Add `do_node_<verb>()` in `src/norn.c`
2. Add `if (strcmp(sub, "<verb>") == 0)` in the `norn node` dispatch
3. Add `<verb>  <description>` to the `node_help:` block
4. Add IPC handler in `serve_client()` in `src/nornd/main.c`
5. If the handler needs access to `norn_client_t` or `time_t`, add fields to `serve_ctx_t`

### Adding a new peer subcommand
1. Add to `do_peer()` in `src/norn.c`
2. Add to `peer_help:` block
3. If it uses IPC, add handler in `serve_client()`
4. If it dials a peer directly, use `nornd_cli_peer()` from `cli_peer.c`

### Adding a new cluster subcommand
1. Add to the cluster dispatch in `src/norn.c` (the inline block)
2. The IPC goes through `nornd_cli_cluster()` which is already generic — just needs the op recognized by `nornd_dispatch()` in `dispatch.c`