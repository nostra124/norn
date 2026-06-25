//! Minimal example: generate an identity, create a node, print its DHT id.
//!
//! Run against an in-tree build:
//!     NORN_LIB_DIR=../../.libs LD_LIBRARY_PATH=../../.libs \
//!         cargo run --example node_id

use norn::{Client, Keypair};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let keypair = Keypair::generate()?;
    let mut client = Client::new(&keypair)?;

    let id = client.id();
    print!("node id: ");
    for b in id {
        print!("{b:02x}");
    }
    println!();
    println!("udp fd:  {}", client.fd());

    // Join the DHT (non-blocking); drive a few ticks.
    client.bootstrap()?;
    for _ in 0..3 {
        client.tick();
    }
    Ok(())
}
