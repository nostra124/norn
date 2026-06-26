# norn — Rust bindings (FEAT-019)

Idiomatic Rust bindings to **libnorn**: *"TCP/UDP addressed by public key instead
of IP."* Two crates:

| crate | what |
|-------|------|
| `norn-sys` | Raw FFI declarations + linking (`build.rs`). Hand-written, no bindgen/libclang. |
| `norn`     | Safe, ergonomic wrapper: `Keypair`, `Client`, `Stream`, `Pump`. |

## Status

Implemented and tested (`cargo test`):

- **`Keypair`** — Ed25519 identity (`generate`, key accessors).
- **`Client`** — the node: `new`, `id`, `bootstrap`, `tick`, `fd`. Single-threaded,
  driven from your event loop.
- **`Stream`** — a reliable byte stream implementing `std::io::Read` + `Write`,
  and (with the `tokio` feature) `tokio::io::AsyncRead` + `AsyncWrite`, so
  `axum`/`hyper`-over-norn ride a pubkey-addressed stream like TCP.
- **`Pump`** — the FEAT-018 splice engine exposed with Rust `Endpoint` closures;
  pure, unit-tested through the FFI without a network.

Next increment: safe `dial(pubkey)` / `listen` wrappers over the async session
API, and an end-to-end axum-over-norn example.

## Building

The crates link `libnorn`. Point `build.rs` at it one of two ways:

**Against an in-tree checkout** (build libnorn first: `./autogen.sh && ./configure && make`):

```sh
export NORN_LIB_DIR="$(git rev-parse --show-toplevel)/.libs"
export LD_LIBRARY_PATH="$NORN_LIB_DIR"   # so test/example binaries find the .so
cargo test
cargo test --features tokio
cargo run --example node_id
```

**Against an installed libnorn** (`make install` provides `norn.pc`):

```sh
cargo build           # build.rs falls back to `pkg-config --libs norn`
```

`libnorn.so` records its own `libsodium` dependency, so consumers don't re-link it.

## Example

```rust
use norn::{Client, Keypair};

let kp = Keypair::generate()?;
let mut client = Client::new(&kp)?;
client.bootstrap()?;
loop { client.tick(); /* … */ }
```

## License

MIT OR Apache-2.0.
