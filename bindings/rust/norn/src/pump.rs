// SPDX-License-Identifier: MIT
//! Safe wrapper over the C splice engine (`norn_pump_*`, FEAT-018).
//!
//! A [`Pump`] bidirectionally splices two [`Endpoint`]s — e.g. a TCP socket and
//! a norn [`Stream`](crate::Stream) — propagating half-close/EOF with bounded,
//! backpressured buffers. Endpoints are ordinary Rust values; the bytes never
//! leave the process, so the pump is exercised in unit tests without a network.

use std::os::raw::c_void;

use norn_sys as sys;

/// Result of a single non-blocking [`Endpoint`] read/write.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Io {
    /// `n` bytes were read/written (may be 0 for a write that accepted nothing).
    Ready(usize),
    /// Nothing available right now (would block).
    WouldBlock,
    /// End of stream (read side only).
    Eof,
    /// Fatal error.
    Error,
}

/// One side of a [`Pump`]. All methods are non-blocking.
pub trait Endpoint: Send {
    /// Read up to `buf.len()` bytes.
    fn read(&mut self, buf: &mut [u8]) -> Io;
    /// Write up to `buf.len()` bytes; may accept fewer (backpressure).
    fn write(&mut self, buf: &[u8]) -> Io;
    /// Half-close this endpoint's write side (optional).
    fn shutdown(&mut self) {}
}

/// Status of a [`Pump`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PumpStatus {
    /// Still forwarding.
    Active,
    /// Both directions drained and closed.
    Done,
    /// A fatal endpoint error occurred.
    Error,
}

/// A bidirectional splice between two [`Endpoint`]s.
pub struct Pump {
    raw: *mut sys::norn_pump,
}

// `Box<dyn Endpoint>` is a fat pointer, so it is double-boxed and passed to C as
// a thin `*mut Box<dyn Endpoint>`.
type BoxedEp = Box<dyn Endpoint>;

unsafe extern "C" fn tramp_read(ctx: *mut c_void, buf: *mut u8, cap: usize) -> i32 {
    let ep = &mut *(ctx as *mut BoxedEp);
    let slice = std::slice::from_raw_parts_mut(buf, cap);
    match ep.read(slice) {
        Io::Ready(n) => n as i32,
        Io::WouldBlock => 0,
        Io::Eof => -1,
        Io::Error => -2,
    }
}

unsafe extern "C" fn tramp_write(ctx: *mut c_void, buf: *const u8, len: usize) -> i32 {
    let ep = &mut *(ctx as *mut BoxedEp);
    let slice = std::slice::from_raw_parts(buf, len);
    match ep.write(slice) {
        Io::Ready(n) => n as i32,
        Io::WouldBlock => 0,
        Io::Eof | Io::Error => -2,
    }
}

unsafe extern "C" fn tramp_shutdown(ctx: *mut c_void) {
    let ep = &mut *(ctx as *mut BoxedEp);
    ep.shutdown();
}

unsafe extern "C" fn tramp_close(ctx: *mut c_void) {
    // Reclaim and drop the boxed endpoint.
    drop(Box::from_raw(ctx as *mut BoxedEp));
}

const IO_VTABLE: sys::norn_forward_io_t = sys::norn_forward_io_t {
    read: Some(tramp_read),
    write: Some(tramp_write),
    shutdown: Some(tramp_shutdown),
    close: Some(tramp_close),
};

impl Pump {
    /// Splice endpoint `a` <-> endpoint `b`. `bufsize` is the per-direction
    /// buffer (0 = engine default). Returns `None` on allocation failure.
    pub fn new<A: Endpoint + 'static, B: Endpoint + 'static>(
        a: A,
        b: B,
        bufsize: usize,
    ) -> Option<Pump> {
        let a_ctx = Box::into_raw(Box::new(Box::new(a) as BoxedEp)) as *mut c_void;
        let b_ctx = Box::into_raw(Box::new(Box::new(b) as BoxedEp)) as *mut c_void;
        let raw = unsafe { sys::norn_pump_new(&IO_VTABLE, a_ctx, &IO_VTABLE, b_ctx, bufsize) };
        if raw.is_null() {
            // C did not take ownership; reclaim the boxes ourselves.
            unsafe {
                drop(Box::from_raw(a_ctx as *mut BoxedEp));
                drop(Box::from_raw(b_ctx as *mut BoxedEp));
            }
            return None;
        }
        Some(Pump { raw })
    }

    /// Run one non-blocking iteration; returns the resulting status.
    pub fn drive(&mut self) -> PumpStatus {
        status_from(unsafe { sys::norn_pump_drive(self.raw) })
    }

    /// Current status without driving.
    pub fn status(&self) -> PumpStatus {
        status_from(unsafe { sys::norn_pump_status(self.raw) })
    }

    /// Bytes forwarded so far: `(a_to_b, b_to_a)`.
    pub fn stats(&self) -> (usize, usize) {
        let (mut ab, mut ba) = (0usize, 0usize);
        unsafe { sys::norn_pump_stats(self.raw, &mut ab, &mut ba) };
        (ab, ba)
    }
}

fn status_from(code: i32) -> PumpStatus {
    match code {
        sys::NORN_PUMP_DONE => PumpStatus::Done,
        sys::NORN_PUMP_ERROR => PumpStatus::Error,
        _ => PumpStatus::Active,
    }
}

impl Drop for Pump {
    fn drop(&mut self) {
        // norn_pump_free invokes the close trampoline, dropping both endpoints.
        unsafe { sys::norn_pump_free(self.raw) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    /// In-memory endpoint: a read-source plus a shared write-sink.
    struct MemEp {
        input: Vec<u8>,
        pos: usize,
        eof: bool,
        sink: Arc<Mutex<Vec<u8>>>,
    }

    impl Endpoint for MemEp {
        fn read(&mut self, buf: &mut [u8]) -> Io {
            if self.pos >= self.input.len() {
                return if self.eof { Io::Eof } else { Io::WouldBlock };
            }
            let n = std::cmp::min(buf.len(), self.input.len() - self.pos);
            buf[..n].copy_from_slice(&self.input[self.pos..self.pos + n]);
            self.pos += n;
            Io::Ready(n)
        }
        fn write(&mut self, buf: &[u8]) -> Io {
            self.sink.lock().unwrap().extend_from_slice(buf);
            Io::Ready(buf.len())
        }
    }

    fn mem(input: &str, sink: Arc<Mutex<Vec<u8>>>) -> MemEp {
        MemEp {
            input: input.as_bytes().to_vec(),
            pos: 0,
            eof: true,
            sink,
        }
    }

    fn run(p: &mut Pump) -> PumpStatus {
        for _ in 0..10_000 {
            let s = p.drive();
            if s != PumpStatus::Active {
                return s;
            }
        }
        panic!("pump did not terminate");
    }

    #[test]
    fn splices_bidirectionally_through_ffi() {
        let a_sink = Arc::new(Mutex::new(Vec::new())); // receives B's input
        let b_sink = Arc::new(Mutex::new(Vec::new())); // receives A's input
        let a = mem("ping", a_sink.clone());
        let b = mem("pong", b_sink.clone());

        let mut pump = Pump::new(a, b, 64).expect("pump");
        assert_eq!(run(&mut pump), PumpStatus::Done);

        assert_eq!(&*b_sink.lock().unwrap(), b"ping");
        assert_eq!(&*a_sink.lock().unwrap(), b"pong");
        assert_eq!(pump.stats(), (4, 4));
    }

    #[test]
    fn oneway_with_default_bufsize() {
        let sink = Arc::new(Mutex::new(Vec::new()));
        let a = mem("hello world", sink.clone());
        let b = mem("", sink.clone()); // B sends nothing
        let mut pump = Pump::new(a, b, 0).expect("pump");
        assert_eq!(run(&mut pump), PumpStatus::Done);
        assert_eq!(&*sink.lock().unwrap(), b"hello world");
    }
}
