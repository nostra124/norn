// SPDX-License-Identifier: MIT
//! A norn logical stream as Rust I/O.
//!
//! [`Stream`] wraps a `norn_stream_t` and implements [`std::io::Read`] +
//! [`std::io::Write`]. With the `tokio` feature it also implements
//! `tokio::io::AsyncRead` + `AsyncWrite`, so `axum`/`hyper`-over-norn and other
//! async protocols ride a pubkey-addressed stream the way they ride TCP.
//!
//! Ownership: a [`Stream`] borrows a stream owned by its session; dropping the
//! [`Stream`] does not free the underlying object. Call [`Stream::close`] to
//! send FIN.

use std::io::{self, Read, Write};

use norn_sys as sys;

/// A reliable, ordered, encrypted byte stream over a norn session.
pub struct Stream {
    raw: *mut sys::norn_stream,
}

impl Stream {
    /// Wrap a raw `norn_stream_t`.
    ///
    /// # Safety
    /// `raw` must be a valid, non-null stream that outlives this `Stream`.
    pub unsafe fn from_raw(raw: *mut sys::norn_stream) -> Stream {
        Stream { raw }
    }

    /// Raw handle, for advanced use with [`norn_sys`].
    pub fn as_ptr(&self) -> *mut sys::norn_stream {
        self.raw
    }

    /// Bytes available to read right now.
    pub fn readable(&self) -> usize {
        unsafe { sys::norn_stream_readable(self.raw) }
    }

    /// Whether the peer has closed its side (sent FIN).
    pub fn peer_closed(&self) -> bool {
        unsafe { sys::norn_stream_peer_closed(self.raw) != 0 }
    }

    /// Send FIN (graceful half-close).
    pub fn close(&mut self) -> io::Result<()> {
        let rc = unsafe { sys::norn_stream_close(self.raw) };
        if rc == 0 {
            Ok(())
        } else {
            Err(io::Error::other("norn_stream_close failed"))
        }
    }

    /// Non-blocking read primitive shared by sync and async paths.
    /// `Ok(n)` (n may be 0 = EOF), or `WouldBlock` / other errors.
    fn try_read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let n = unsafe { sys::norn_stream_read(self.raw, buf.as_mut_ptr(), buf.len()) };
        if n > 0 {
            Ok(n as usize)
        } else if n < 0 {
            Err(io::Error::other("norn_stream_read failed"))
        } else if self.peer_closed() {
            Ok(0) // EOF
        } else {
            Err(io::ErrorKind::WouldBlock.into())
        }
    }

    /// Non-blocking write primitive.
    fn try_write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let n = unsafe { sys::norn_stream_write(self.raw, buf.as_ptr(), buf.len()) };
        if n >= 0 {
            Ok(n as usize)
        } else {
            Err(io::Error::other("norn_stream_write failed"))
        }
    }
}

impl Read for Stream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.try_read(buf)
    }
}

impl Write for Stream {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.try_write(buf)
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

// Streams are driven from a single-threaded event loop; the handle is not Sync,
// but may be moved between tasks on one thread.
unsafe impl Send for Stream {}

#[cfg(feature = "tokio")]
mod async_io {
    use super::Stream;
    use std::io;
    use std::pin::Pin;
    use std::task::{Context, Poll};
    use tokio::io::{AsyncRead, AsyncWrite, ReadBuf};

    // norn is non-blocking but has no readiness fd per stream we register here,
    // so on WouldBlock we re-schedule the task. Production integrations should
    // wake from the client's tick loop; this keeps the contract correct.
    impl AsyncRead for Stream {
        fn poll_read(
            mut self: Pin<&mut Self>,
            cx: &mut Context<'_>,
            buf: &mut ReadBuf<'_>,
        ) -> Poll<io::Result<()>> {
            let mut tmp = [0u8; 4096];
            let cap = tmp.len().min(buf.remaining());
            match self.try_read(&mut tmp[..cap]) {
                Ok(n) => {
                    buf.put_slice(&tmp[..n]);
                    Poll::Ready(Ok(()))
                }
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    cx.waker().wake_by_ref();
                    Poll::Pending
                }
                Err(e) => Poll::Ready(Err(e)),
            }
        }
    }

    impl AsyncWrite for Stream {
        fn poll_write(
            mut self: Pin<&mut Self>,
            cx: &mut Context<'_>,
            buf: &[u8],
        ) -> Poll<io::Result<usize>> {
            match self.try_write(buf) {
                Ok(0) => {
                    cx.waker().wake_by_ref();
                    Poll::Pending
                }
                Ok(n) => Poll::Ready(Ok(n)),
                Err(e) => Poll::Ready(Err(e)),
            }
        }
        fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<io::Result<()>> {
            Poll::Ready(Ok(()))
        }
        fn poll_shutdown(mut self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<io::Result<()>> {
            Poll::Ready(self.close())
        }
    }
}
