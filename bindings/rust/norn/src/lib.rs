//! Safe, idiomatic Rust bindings to **libnorn** (FEAT-019).
//!
//! norn provides "TCP/UDP addressed by **public key** instead of IP": you hand
//! it a peer's pubkey and get an encrypted, reliable stream, with discovery and
//! NAT traversal handled for you.
//!
//! This crate wraps the C SDK ([`norn_sys`]) in memory-safe types:
//! - [`Keypair`] — Ed25519 identity.
//! - [`Client`] — the event-loop-driven node ([`Client::tick`]).
//! - [`Stream`] — a reliable byte stream implementing [`std::io::Read`] +
//!   [`std::io::Write`] (and, with the `tokio` feature, `AsyncRead`/`AsyncWrite`).
//! - [`Pump`] — the transport-agnostic splice engine (FEAT-018) exposed with
//!   Rust [`Endpoint`] closures; pure and unit-tested without a network.
//!
//! # Threading
//! Like libnorn, a [`Client`] is single-threaded: create it on one thread and
//! drive it with [`Client::tick`] from your event loop.
#![deny(rust_2018_idioms)]

use std::os::raw::c_char;
use std::ptr;

use norn_sys as sys;

mod pump;
mod session;
mod stream;

pub use pump::{Endpoint, Io, Pump, PumpStatus};
pub use session::{Session, SessionState};
pub use stream::Stream;

/// Public-key length in bytes (Ed25519).
pub const PUBKEY_BYTES: usize = sys::NORN_PUBKEY_BYTES;
/// Node-id length in bytes.
pub const ID_BYTES: usize = sys::NORN_ID_BYTES;

/// An Ed25519 keypair (identity).
pub struct Keypair(sys::keypair_t);

impl Keypair {
    /// Generate a fresh keypair.
    pub fn generate() -> Result<Keypair, Error> {
        // crypto_init() is idempotent; ensures libsodium is ready.
        unsafe { sys::crypto_init() };
        let mut kp = sys::keypair_t {
            public_key: [0u8; 32],
            secret_key: [0u8; 64],
        };
        let rc = unsafe { sys::crypto_keypair_new(&mut kp) };
        if rc != 0 {
            return Err(Error::Crypto);
        }
        Ok(Keypair(kp))
    }

    /// The 32-byte public key.
    pub fn public_key(&self) -> &[u8; 32] {
        &self.0.public_key
    }

    /// The 64-byte secret key.
    pub fn secret_key(&self) -> &[u8; 64] {
        &self.0.secret_key
    }
}

/// A norn node. Drive it with [`Client::tick`] from your event loop.
pub struct Client {
    raw: *mut sys::norn_client,
    // Owns the boxed dial/listen closures so they outlive the async callbacks.
    callbacks: Vec<Box<dyn std::any::Any>>,
}

impl Client {
    /// Create a node bound to the given identity.
    pub fn new(keypair: &Keypair) -> Result<Client, Error> {
        // Borrowed for the duration of the call only (norn copies what it needs).
        let version = b"norn-rs/0.1\0";
        let cfg = sys::norn_config_t {
            version: version.as_ptr() as *const c_char,
            read_only: 0,
            private_mode: 0,
            boot_ips: ptr::null(),
            boot_ports: ptr::null(),
            boot_count: 0,
            log_func: ptr::null_mut(),
        };
        let raw = unsafe {
            sys::norn_new(
                keypair.public_key().as_ptr(),
                keypair.secret_key().as_ptr(),
                &cfg,
            )
        };
        if raw.is_null() {
            return Err(Error::Init);
        }
        Ok(Client {
            raw,
            callbacks: Vec::new(),
        })
    }

    /// Dial a peer at a known endpoint, identified by its public key (async).
    /// `cb` is invoked from [`Client::tick`] on each session state change.
    ///
    /// (Endpoint dialing; DHT-resolved `dial(pubkey)` arrives with the session
    /// resolve path.)
    pub fn dial_direct<F>(
        &mut self,
        ip: u32,
        port: u16,
        pubkey: &[u8; PUBKEY_BYTES],
        cb: F,
    ) -> Result<(), Error>
    where
        F: FnMut(&Session, SessionState) + 'static,
    {
        let ud = session::store_dial_cb(&mut self.callbacks, cb);
        let ep = sys::norn_direct_endpoint_t { ip, port };
        let rc = unsafe {
            sys::norn_dial_direct_async(
                self.raw,
                &ep,
                pubkey.as_ptr(),
                std::ptr::null(),
                session::dial_trampoline,
                ud,
            )
        };
        if rc == 0 {
            Ok(())
        } else {
            Err(Error::Op)
        }
    }

    /// Listen for inbound sessions (async). `cb` is invoked from
    /// [`Client::tick`] for each accepted session.
    pub fn listen<F>(&mut self, port: u16, cb: F) -> Result<(), Error>
    where
        F: FnMut(Session) + 'static,
    {
        let ud = session::store_accept_cb(&mut self.callbacks, cb);
        let rc = unsafe {
            sys::norn_listen_async(
                self.raw,
                port,
                std::ptr::null(),
                session::accept_trampoline,
                ud,
            )
        };
        if rc == 0 {
            Ok(())
        } else {
            Err(Error::Op)
        }
    }

    /// This node's 20-byte DHT id.
    pub fn id(&self) -> [u8; ID_BYTES] {
        let mut out = [0u8; ID_BYTES];
        // Non-null self ⇒ success.
        unsafe { sys::norn_get_id(self.raw, out.as_mut_ptr()) };
        out
    }

    /// Begin bootstrapping into the DHT (non-blocking).
    pub fn bootstrap(&mut self) -> Result<(), Error> {
        let rc = unsafe { sys::norn_bootstrap(self.raw) };
        if rc == 0 {
            Ok(())
        } else {
            Err(Error::Op)
        }
    }

    /// Process pending I/O, timers and callbacks. Call regularly.
    /// Returns the number of events processed.
    pub fn tick(&mut self) -> i32 {
        unsafe { sys::norn_tick(self.raw) }
    }

    /// The underlying UDP socket fd, for `poll`/`select` integration.
    pub fn fd(&self) -> i32 {
        unsafe { sys::norn_get_fd(self.raw) }
    }

    /// Raw handle, for advanced use with [`norn_sys`].
    pub fn as_ptr(&self) -> *mut sys::norn_client {
        self.raw
    }
}

impl Drop for Client {
    fn drop(&mut self) {
        unsafe { sys::norn_free(self.raw) };
    }
}

/// Errors surfaced by the safe API.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// libsodium / key generation failed.
    Crypto,
    /// Client creation failed (bad params or socket bind).
    Init,
    /// An operation returned an error code.
    Op,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let s = match self {
            Error::Crypto => "crypto/keypair failure",
            Error::Init => "client initialisation failed",
            Error::Op => "operation failed",
        };
        f.write_str(s)
    }
}

impl std::error::Error for Error {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keypair_is_nonzero_and_distinct() {
        let a = Keypair::generate().expect("keygen");
        let b = Keypair::generate().expect("keygen");
        assert_ne!(a.public_key(), &[0u8; 32]);
        assert_ne!(a.public_key(), b.public_key());
    }

    #[test]
    fn client_lifecycle_and_id() {
        let kp = Keypair::generate().expect("keygen");
        let client = Client::new(&kp).expect("client");
        let id = client.id();
        assert_ne!(id, [0u8; ID_BYTES]);
        // fd is a real, open socket.
        assert!(client.fd() >= 0);
        // dropped here → norn_free
    }

    #[test]
    fn dial_and_listen_register() {
        // Exercises the dial/listen API surface and callback ownership: register
        // both, tick (no peer ⇒ no callbacks fire), and drop cleanly.
        let kp = Keypair::generate().expect("keygen");
        let mut client = Client::new(&kp).expect("client");
        client
            .listen(0, |sess| {
                let _ = sess.state();
            })
            .expect("listen");
        let peer = [7u8; PUBKEY_BYTES];
        client
            .dial_direct(0x7f000001, 9999, &peer, |sess, state| {
                let _ = (sess.state(), state);
            })
            .expect("dial");
        for _ in 0..3 {
            client.tick();
        }
        assert_eq!(client.callbacks.len(), 2);
    }
}
