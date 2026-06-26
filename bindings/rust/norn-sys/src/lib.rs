//! Raw FFI bindings to libnorn — the C SDK (FEAT-019).
//!
//! Hand-written (no bindgen, so no libclang build dependency). Only the surface
//! the safe `norn` crate needs is declared. All items are `unsafe` to use; the
//! ergonomic, memory-safe API lives in the `norn` crate.
#![allow(non_camel_case_types)]

use std::os::raw::{c_char, c_int, c_void};

pub const NORN_ID_BYTES: usize = 20;
pub const NORN_PUBKEY_BYTES: usize = 32;
pub const NORN_SECRETKEY_BYTES: usize = 64;

/* Opaque handles. */
#[repr(C)]
pub struct norn_client {
    _private: [u8; 0],
}
#[repr(C)]
pub struct norn_session {
    _private: [u8; 0],
}
#[repr(C)]
pub struct norn_stream {
    _private: [u8; 0],
}
#[repr(C)]
pub struct norn_pump {
    _private: [u8; 0],
}

#[repr(C)]
pub struct keypair_t {
    pub public_key: [u8; 32],
    pub secret_key: [u8; 64],
}

#[repr(C)]
pub struct norn_config_t {
    pub version: *const c_char,
    pub read_only: c_int,
    pub private_mode: c_int,
    pub boot_ips: *const u32,
    pub boot_ports: *const u16,
    pub boot_count: c_int,
    /// `void (*log_func)(const char*, ...)` — kept opaque (always pass null).
    pub log_func: *mut c_void,
}

#[repr(C)]
pub struct norn_direct_endpoint_t {
    pub ip: u32,
    pub port: u16,
}

/* Session/stream state enums are plain ints across the boundary. */
pub const NORN_SESSION_RESOLVING: c_int = 0;
pub const NORN_SESSION_CONNECTING: c_int = 1;
pub const NORN_SESSION_ESTABLISHED: c_int = 2;
pub const NORN_SESSION_CLOSING: c_int = 3;
pub const NORN_SESSION_CLOSED: c_int = 4;

pub const NORN_STREAM_READY: c_int = 0;
pub const NORN_STREAM_CLOSED: c_int = 1;
pub const NORN_STREAM_RESET: c_int = 2;

pub const NORN_PUMP_ACTIVE: c_int = 0;
pub const NORN_PUMP_DONE: c_int = 1;
pub const NORN_PUMP_ERROR: c_int = 2;

pub type norn_session_callback_t =
    unsafe extern "C" fn(session: *mut norn_session, state: c_int, user_data: *mut c_void);
pub type norn_accept_callback_t =
    unsafe extern "C" fn(session: *mut norn_session, user_data: *mut c_void);
pub type norn_stream_callback_t =
    unsafe extern "C" fn(stream: *mut norn_stream, state: c_int, user_data: *mut c_void);

/// `norn_forward_io_t` — one endpoint of a pump.
#[repr(C)]
pub struct norn_forward_io_t {
    pub read: Option<unsafe extern "C" fn(ctx: *mut c_void, buf: *mut u8, cap: usize) -> c_int>,
    pub write: Option<unsafe extern "C" fn(ctx: *mut c_void, buf: *const u8, len: usize) -> c_int>,
    pub shutdown: Option<unsafe extern "C" fn(ctx: *mut c_void)>,
    pub close: Option<unsafe extern "C" fn(ctx: *mut c_void)>,
}

/* === cluster KV (FEAT-024/025/026) === */

pub const NORN_CLUSTER_PUBKEY: usize = 32;

/* norn_node_class_t */
pub const NORN_NODE_MOBILE: c_int = 0;
pub const NORN_NODE_LAPTOP: c_int = 1;
pub const NORN_NODE_WORKSTATION: c_int = 2;
pub const NORN_NODE_SERVER: c_int = 3;

#[repr(C)]
pub struct norn_cluster {
    _private: [u8; 0],
}

/// Transport vtable: how the cluster sends a wire frame to a member by pubkey.
pub type norn_cluster_send_fn = unsafe extern "C" fn(
    ctx: *mut c_void,
    pubkey: *const u8, /* [NORN_CLUSTER_PUBKEY] */
    data: *const u8,
    len: usize,
);

#[repr(C)]
pub struct norn_cluster_io_t {
    pub send: Option<norn_cluster_send_fn>,
    pub ctx: *mut c_void,
}

#[repr(C)]
pub struct norn_cluster_config_t {
    pub self_class: c_int,
    pub uptime_score: u32,
    pub election_eligible: c_int,
    pub max_kv_entries: c_int,
    pub election_base_ms: u32,
    pub election_spread_ms: u32,
    pub heartbeat_ms: u32,
}

extern "C" {
    /* crypto */
    pub fn crypto_init() -> c_int;
    pub fn crypto_keypair_new(kp: *mut keypair_t) -> c_int;

    /* client lifecycle */
    pub fn norn_new(
        self_pub: *const u8,
        self_sec: *const u8,
        cfg: *const norn_config_t,
    ) -> *mut norn_client;
    pub fn norn_free(client: *mut norn_client);
    pub fn norn_get_id(client: *const norn_client, out: *mut u8) -> c_int;
    pub fn norn_bootstrap(client: *mut norn_client) -> c_int;
    pub fn norn_tick(client: *mut norn_client) -> c_int;
    pub fn norn_get_fd(client: *const norn_client) -> c_int;

    /* sessions / streams */
    pub fn norn_dial_direct_async(
        client: *mut norn_client,
        endpoint: *const norn_direct_endpoint_t,
        pubkey: *const u8,
        suite: *const c_void,
        callback: norn_session_callback_t,
        user_data: *mut c_void,
    ) -> c_int;
    pub fn norn_listen_async(
        client: *mut norn_client,
        port: u16,
        suite: *const c_void,
        callback: norn_accept_callback_t,
        user_data: *mut c_void,
    ) -> c_int;
    pub fn norn_stream_open_async(
        session: *mut norn_session,
        callback: norn_stream_callback_t,
        user_data: *mut c_void,
    ) -> *mut norn_stream;
    pub fn norn_session_set_accept_stream(
        session: *mut norn_session,
        callback: Option<unsafe extern "C" fn(stream: *mut norn_stream, user_data: *mut c_void)>,
        user_data: *mut c_void,
    ) -> c_int;
    pub fn norn_stream_write(stream: *mut norn_stream, data: *const u8, len: usize) -> c_int;
    pub fn norn_stream_read(stream: *mut norn_stream, buf: *mut u8, cap: usize) -> c_int;
    pub fn norn_stream_readable(stream: *const norn_stream) -> usize;
    pub fn norn_stream_close(stream: *mut norn_stream) -> c_int;
    pub fn norn_stream_peer_closed(stream: *const norn_stream) -> c_int;
    pub fn norn_session_get_state(session: *const norn_session) -> c_int;
    pub fn norn_session_get_peer(session: *const norn_session, pubkey: *mut u8) -> c_int;

    /* forward / splice engine (FEAT-018) */
    pub fn norn_pump_new(
        a: *const norn_forward_io_t,
        a_ctx: *mut c_void,
        b: *const norn_forward_io_t,
        b_ctx: *mut c_void,
        bufsize: usize,
    ) -> *mut norn_pump;
    pub fn norn_pump_drive(p: *mut norn_pump) -> c_int;
    pub fn norn_pump_status(p: *const norn_pump) -> c_int;
    pub fn norn_pump_stats(p: *const norn_pump, a_to_b: *mut usize, b_to_a: *mut usize);
    pub fn norn_pump_free(p: *mut norn_pump);

    /* cluster KV (FEAT-024/025/026) */
    pub fn norn_cluster_new(
        self_pubkey: *const u8,
        io: *const norn_cluster_io_t,
        cfg: *const norn_cluster_config_t,
    ) -> *mut norn_cluster;
    pub fn norn_cluster_free(cl: *mut norn_cluster);
    pub fn norn_cluster_add_member(
        cl: *mut norn_cluster,
        pubkey: *const u8,
        cls: c_int,
        eligible: c_int,
    ) -> c_int;
    pub fn norn_cluster_bootstrap(
        cl: *mut norn_cluster,
        peer_pubkeys: *const u8,
        n_peers: c_int,
    ) -> c_int;
    pub fn norn_cluster_promote(cl: *mut norn_cluster, pubkey: *const u8) -> c_int;
    pub fn norn_cluster_remove(cl: *mut norn_cluster, pubkey: *const u8) -> c_int;
    pub fn norn_cluster_tick(cl: *mut norn_cluster, now_ms: u64);
    pub fn norn_cluster_input(
        cl: *mut norn_cluster,
        from_pubkey: *const u8,
        data: *const u8,
        len: usize,
    );
    pub fn norn_cluster_kv_put(
        cl: *mut norn_cluster,
        key: *const u8,
        klen: usize,
        val: *const u8,
        vlen: usize,
    ) -> c_int;
    pub fn norn_cluster_kv_del(cl: *mut norn_cluster, key: *const u8, klen: usize) -> c_int;
    pub fn norn_cluster_kv_get(
        cl: *mut norn_cluster,
        key: *const u8,
        klen: usize,
        out: *mut u8,
        cap: usize,
    ) -> c_int;
    pub fn norn_cluster_is_leader(cl: *const norn_cluster) -> c_int;
    pub fn norn_cluster_leader(cl: *const norn_cluster) -> *const u8;
    pub fn norn_cluster_member_count(cl: *const norn_cluster) -> c_int;
    pub fn norn_cluster_members(cl: *const norn_cluster, out: *mut u8, max: c_int) -> c_int;
    pub fn norn_cluster_is_voter(cl: *const norn_cluster, pubkey: *const u8) -> c_int;
}
