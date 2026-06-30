// SPDX-License-Identifier: MIT
//! Service-over-pubkey echo tunnel (FEAT-018/019).
//!
//! Demonstrates the safe stream API end to end:
//!   - the server listens, and for each inbound stream echoes bytes back;
//!   - the client dials by endpoint+pubkey, opens a stream, writes, reads.
//!
//! Run against an in-tree build:
//!     NORN_LIB_DIR=../../.libs LD_LIBRARY_PATH=../../.libs \
//!         cargo run --example tunnel
//!
//! (This wires the API; a live two-peer round-trip is exercised by the C
//! loopback test and PIT.)

use std::io::{Read, Write};

use norn::{Client, Keypair, SessionState};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // --- server: echo every inbound stream ---
    let server_kp = Keypair::generate()?;
    let mut server = Client::new(&server_kp)?;
    server.listen(0, |session| {
        session.on_inbound_stream(|mut stream| {
            let mut buf = [0u8; 1024];
            if let Ok(n) = stream.read(&mut buf) {
                let _ = stream.write(&buf[..n]); // echo
            }
        });
    })?;

    // --- client: dial the server and open a stream ---
    let client_kp = Keypair::generate()?;
    let mut client = Client::new(&client_kp)?;
    let server_pub = *server_kp.public_key();
    client.dial_direct(0x7f000001, 0, &server_pub, |session, state| {
        if state == SessionState::Established {
            if let Some(mut stream) = session.open_stream() {
                let _ = stream.write(b"hello over a pubkey stream");
            }
        }
    })?;

    // Drive both event loops.
    for _ in 0..100 {
        server.tick();
        client.tick();
    }
    println!("tunnel example: wired (server listening, client dialed)");
    Ok(())
}
