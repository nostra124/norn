// SPDX-License-Identifier: MIT
//! Sessions and the dial/listen callback plumbing (FEAT-019).
//!
//! A [`Session`] is a verified, encrypted connection to a peer. Open logical
//! [`Stream`](crate::Stream)s over it. Sessions are owned by the [`Client`];
//! a `Session` handle borrows one and does not free it on drop.

use std::os::raw::{c_int, c_void};

use norn_sys as sys;

use crate::Stream;

/// Session lifecycle state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionState {
    Resolving,
    Connecting,
    Established,
    Closing,
    Closed,
}

impl From<c_int> for SessionState {
    fn from(v: c_int) -> Self {
        match v {
            sys::NORN_SESSION_RESOLVING => SessionState::Resolving,
            sys::NORN_SESSION_CONNECTING => SessionState::Connecting,
            sys::NORN_SESSION_ESTABLISHED => SessionState::Established,
            sys::NORN_SESSION_CLOSING => SessionState::Closing,
            _ => SessionState::Closed,
        }
    }
}

/// A verified, encrypted session with a peer. Borrows a session owned by the
/// [`Client`](crate::Client).
pub struct Session {
    raw: *mut sys::norn_session,
}

impl Session {
    pub(crate) unsafe fn from_raw(raw: *mut sys::norn_session) -> Session {
        Session { raw }
    }

    /// Current state.
    pub fn state(&self) -> SessionState {
        SessionState::from(unsafe { sys::norn_session_get_state(self.raw) })
    }

    /// The verified peer public key (valid once `Established`).
    pub fn peer(&self) -> [u8; crate::PUBKEY_BYTES] {
        let mut out = [0u8; crate::PUBKEY_BYTES];
        unsafe { sys::norn_session_get_peer(self.raw, out.as_mut_ptr()) };
        out
    }

    /// Open a logical stream over this session. Returns `None` on error.
    pub fn open_stream(&self) -> Option<Stream> {
        // No per-stream readiness callback is wired here; poll the stream.
        let s =
            unsafe { sys::norn_stream_open_async(self.raw, noop_stream_cb, std::ptr::null_mut()) };
        if s.is_null() {
            None
        } else {
            Some(unsafe { Stream::from_raw(s) })
        }
    }

    /// Register a handler for inbound (peer-initiated) streams — the server side
    /// of a tunnel. `cb` is invoked from `Client::tick` for each stream the peer
    /// opens on this session.
    ///
    /// Note: the closure is leaked to live for the session's lifetime (sessions
    /// are owned by the `Client`); call once per session.
    pub fn on_inbound_stream<F>(&self, cb: F)
    where
        F: FnMut(Stream) + 'static,
    {
        let boxed: Box<Box<dyn FnMut(Stream)>> = Box::new(Box::new(cb));
        let ud = Box::into_raw(boxed) as *mut c_void;
        unsafe { sys::norn_session_set_accept_stream(self.raw, Some(inbound_trampoline), ud) };
    }

    /// Raw handle for advanced use.
    pub fn as_ptr(&self) -> *mut sys::norn_session {
        self.raw
    }
}

unsafe extern "C" fn inbound_trampoline(stream: *mut sys::norn_stream, ud: *mut c_void) {
    if ud.is_null() {
        return;
    }
    let cb = &mut *(ud as *mut Box<dyn FnMut(Stream)>);
    cb(Stream::from_raw(stream));
}

unsafe extern "C" fn noop_stream_cb(_s: *mut sys::norn_stream, _state: c_int, _ud: *mut c_void) {}

// Boxed closure types stored in the Client.
type DialCb = Box<dyn FnMut(&Session, SessionState)>;
type AcceptCb = Box<dyn FnMut(Session)>;

/// Box a dial closure, store it in `sink` (so it outlives the async callback),
/// and return a stable `void*` to it.
pub(crate) fn store_dial_cb<F>(sink: &mut Vec<Box<dyn std::any::Any>>, cb: F) -> *mut c_void
where
    F: FnMut(&Session, SessionState) + 'static,
{
    let boxed: Box<DialCb> = Box::new(Box::new(cb));
    let ptr = (&*boxed as *const DialCb) as *mut c_void;
    sink.push(boxed);
    ptr
}

pub(crate) fn store_accept_cb<F>(sink: &mut Vec<Box<dyn std::any::Any>>, cb: F) -> *mut c_void
where
    F: FnMut(Session) + 'static,
{
    let boxed: Box<AcceptCb> = Box::new(Box::new(cb));
    let ptr = (&*boxed as *const AcceptCb) as *mut c_void;
    sink.push(boxed);
    ptr
}

pub(crate) unsafe extern "C" fn dial_trampoline(
    session: *mut sys::norn_session,
    state: c_int,
    ud: *mut c_void,
) {
    if ud.is_null() {
        return;
    }
    let cb = &mut *(ud as *mut DialCb);
    let s = Session::from_raw(session);
    cb(&s, SessionState::from(state));
}

pub(crate) unsafe extern "C" fn accept_trampoline(
    session: *mut sys::norn_session,
    ud: *mut c_void,
) {
    if ud.is_null() {
        return;
    }
    let cb = &mut *(ud as *mut AcceptCb);
    cb(Session::from_raw(session));
}
