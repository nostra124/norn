//! Safe wrapper over libnorn's class-aware Raft cluster KV (FEAT-024/025/026).
//!
//! A [`Cluster`] is a pubkey-addressed replicated key-value store. Wire frames
//! between members are delivered through a caller-supplied `send` closure (so the
//! transport — a norn session/stream, a test loopback, … — stays the embedder's
//! concern); inbound frames are fed back in with [`Cluster::input`]. Time is
//! driven by [`Cluster::tick`]. Quorum rides the **voters** (servers) only, so a
//! mostly-offline fleet of learners never stalls writes.

use std::os::raw::c_void;

use norn_sys as sys;

use crate::{Error, PUBKEY_BYTES};

/// A member's availability class — drives its default voter/learner role.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NodeClass {
    Mobile,
    Laptop,
    Workstation,
    Server,
}

impl NodeClass {
    fn raw(self) -> std::os::raw::c_int {
        match self {
            NodeClass::Mobile => sys::NORN_NODE_MOBILE,
            NodeClass::Laptop => sys::NORN_NODE_LAPTOP,
            NodeClass::Workstation => sys::NORN_NODE_WORKSTATION,
            NodeClass::Server => sys::NORN_NODE_SERVER,
        }
    }
}

/// Boxed send closure, kept alive for the cluster's lifetime and reached from the
/// C `send` trampoline via its `ctx` pointer.
struct SendBox {
    cb: Box<dyn FnMut(&[u8; PUBKEY_BYTES], &[u8])>,
}

unsafe extern "C" fn send_trampoline(
    ctx: *mut c_void,
    pubkey: *const u8,
    data: *const u8,
    len: usize,
) {
    if ctx.is_null() || pubkey.is_null() {
        return;
    }
    let sb = &mut *(ctx as *mut SendBox);
    let pk = &*(pubkey as *const [u8; PUBKEY_BYTES]);
    let bytes = if data.is_null() || len == 0 {
        &[][..]
    } else {
        std::slice::from_raw_parts(data, len)
    };
    (sb.cb)(pk, bytes);
}

/// A class-aware Raft cluster node + its replicated KV store.
pub struct Cluster {
    raw: *mut sys::norn_cluster,
    /// Kept alive: the C side holds a pointer into this box.
    _send: Box<SendBox>,
}

impl Cluster {
    /// Create a cluster node identified by `self_pubkey`, of class `class`. The
    /// node is added as its own first member. `send` is invoked (from
    /// [`tick`](Cluster::tick) / KV calls) to deliver a wire frame to a peer.
    pub fn new<F>(self_pubkey: &[u8; PUBKEY_BYTES], class: NodeClass, send: F) -> Result<Cluster, Error>
    where
        F: FnMut(&[u8; PUBKEY_BYTES], &[u8]) + 'static,
    {
        let mut sb = Box::new(SendBox { cb: Box::new(send) });
        let io = sys::norn_cluster_io_t {
            send: Some(send_trampoline),
            ctx: (&mut *sb as *mut SendBox) as *mut c_void,
        };
        let cfg = sys::norn_cluster_config_t {
            self_class: class.raw(),
            uptime_score: 0,
            election_eligible: -1, /* derive from class */
            max_kv_entries: 0,     /* default */
            election_base_ms: 0,
            election_spread_ms: 0,
            heartbeat_ms: 0,
        };
        let raw = unsafe { sys::norn_cluster_new(self_pubkey.as_ptr(), &io, &cfg) };
        if raw.is_null() {
            return Err(Error::Op);
        }
        Ok(Cluster { raw, _send: sb })
    }

    /// Add a member of class `class` (SERVER → voter, else learner). `eligible`
    /// forces candidacy eligibility (use `-1` to derive from class).
    pub fn add_member(&mut self, pubkey: &[u8; PUBKEY_BYTES], class: NodeClass, eligible: i32) -> Result<(), Error> {
        rc(unsafe { sys::norn_cluster_add_member(self.raw, pubkey.as_ptr(), class.raw(), eligible) })
    }

    /// Form a cluster from a set of server peers (all voters, plus self).
    pub fn bootstrap(&mut self, peer_pubkeys: &[[u8; PUBKEY_BYTES]]) -> Result<(), Error> {
        rc(unsafe {
            sys::norn_cluster_bootstrap(
                self.raw,
                peer_pubkeys.as_ptr() as *const u8,
                peer_pubkeys.len() as std::os::raw::c_int,
            )
        })
    }

    /// Promote a learner to a voting member.
    pub fn promote(&mut self, pubkey: &[u8; PUBKEY_BYTES]) -> Result<(), Error> {
        rc(unsafe { sys::norn_cluster_promote(self.raw, pubkey.as_ptr()) })
    }

    /// Remove a member.
    pub fn remove(&mut self, pubkey: &[u8; PUBKEY_BYTES]) -> Result<(), Error> {
        rc(unsafe { sys::norn_cluster_remove(self.raw, pubkey.as_ptr()) })
    }

    /// Advance time (drives elections/heartbeats and applies committed entries).
    pub fn tick(&mut self, now_ms: u64) {
        unsafe { sys::norn_cluster_tick(self.raw, now_ms) }
    }

    /// Feed an inbound wire frame received from `from`.
    pub fn input(&mut self, from: &[u8; PUBKEY_BYTES], data: &[u8]) {
        unsafe { sys::norn_cluster_input(self.raw, from.as_ptr(), data.as_ptr(), data.len()) }
    }

    /// Propose a PUT (forwarded to the leader if we are not it).
    pub fn put(&mut self, key: &[u8], val: &[u8]) -> Result<(), Error> {
        rc(unsafe {
            sys::norn_cluster_kv_put(self.raw, key.as_ptr(), key.len(), val.as_ptr(), val.len())
        })
    }

    /// Propose a DEL.
    pub fn del(&mut self, key: &[u8]) -> Result<(), Error> {
        rc(unsafe { sys::norn_cluster_kv_del(self.raw, key.as_ptr(), key.len()) })
    }

    /// Propose a compare-and-set: set `key` to `val` iff its current value equals
    /// `expect` (an empty `expect` matches an absent key). The condition is
    /// evaluated on apply by every replica, so it is linearizable — the basis for
    /// single-owner claims. Returns `Ok` if accepted (proposed/forwarded); the
    /// claim's *success* is observed by reading the key back after it commits.
    pub fn cas(&mut self, key: &[u8], expect: &[u8], val: &[u8]) -> Result<(), Error> {
        rc(unsafe {
            sys::norn_cluster_kv_cas(
                self.raw,
                key.as_ptr(),
                key.len(),
                expect.as_ptr(),
                expect.len(),
                val.as_ptr(),
                val.len(),
            )
        })
    }

    /// Local read of the replicated map. Returns the value, or `None` if absent.
    pub fn get(&self, key: &[u8]) -> Option<Vec<u8>> {
        // First sizing call with a generous buffer; the C API returns the length
        // or -1. KV values are bounded, so one reasonably-sized buffer suffices.
        let mut buf = vec![0u8; 4096];
        let n = unsafe {
            sys::norn_cluster_kv_get(self.raw as *mut _, key.as_ptr(), key.len(), buf.as_mut_ptr(), buf.len())
        };
        if n < 0 {
            return None;
        }
        buf.truncate(n as usize);
        Some(buf)
    }

    /// Whether this node is the current leader.
    pub fn is_leader(&self) -> bool {
        unsafe { sys::norn_cluster_is_leader(self.raw) != 0 }
    }

    /// The current leader's public key, or `None` if unknown.
    pub fn leader(&self) -> Option<[u8; PUBKEY_BYTES]> {
        let p = unsafe { sys::norn_cluster_leader(self.raw) };
        if p.is_null() {
            return None;
        }
        let mut out = [0u8; PUBKEY_BYTES];
        unsafe { std::ptr::copy_nonoverlapping(p, out.as_mut_ptr(), PUBKEY_BYTES) };
        Some(out)
    }

    /// Number of members (voters + learners).
    pub fn member_count(&self) -> usize {
        let n = unsafe { sys::norn_cluster_member_count(self.raw) };
        if n < 0 {
            0
        } else {
            n as usize
        }
    }

    /// The member public keys.
    pub fn members(&self) -> Vec<[u8; PUBKEY_BYTES]> {
        let max = self.member_count();
        if max == 0 {
            return Vec::new();
        }
        let mut flat = vec![0u8; max * PUBKEY_BYTES];
        let n = unsafe {
            sys::norn_cluster_members(self.raw, flat.as_mut_ptr(), max as std::os::raw::c_int)
        };
        if n < 0 {
            return Vec::new();
        }
        (0..n as usize)
            .map(|i| {
                let mut pk = [0u8; PUBKEY_BYTES];
                pk.copy_from_slice(&flat[i * PUBKEY_BYTES..(i + 1) * PUBKEY_BYTES]);
                pk
            })
            .collect()
    }

    /// Whether `pubkey` is a voting member.
    pub fn is_voter(&self, pubkey: &[u8; PUBKEY_BYTES]) -> bool {
        unsafe { sys::norn_cluster_is_voter(self.raw, pubkey.as_ptr()) != 0 }
    }
}

impl Drop for Cluster {
    fn drop(&mut self) {
        unsafe { sys::norn_cluster_free(self.raw) };
    }
}

fn rc(code: std::os::raw::c_int) -> Result<(), Error> {
    if code == 0 {
        Ok(())
    } else {
        Err(Error::Op)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn single_server_node_elects_and_serves_kv() {
        let pk = [7u8; PUBKEY_BYTES];
        // single-node SERVER: it is its own voter (quorum 1).
        let mut cl = Cluster::new(&pk, NodeClass::Server, |_pk, _frame| {}).unwrap();
        assert_eq!(cl.member_count(), 1);
        assert!(cl.is_voter(&pk));

        // drive timers until it elects itself leader.
        let mut t = 0u64;
        for _ in 0..200 {
            t += 20;
            cl.tick(t);
            if cl.is_leader() {
                break;
            }
        }
        assert!(cl.is_leader(), "single voter should self-elect");
        assert_eq!(cl.leader(), Some(pk));

        // put → commit (on subsequent ticks) → local get.
        cl.put(b"greet", b"hello").unwrap();
        for _ in 0..50 {
            t += 20;
            cl.tick(t);
        }
        assert_eq!(cl.get(b"greet").as_deref(), Some(&b"hello"[..]));
        assert!(cl.get(b"missing").is_none());

        // delete removes it.
        cl.del(b"greet").unwrap();
        for _ in 0..50 {
            t += 20;
            cl.tick(t);
        }
        assert!(cl.get(b"greet").is_none());
    }

    #[test]
    fn cas_claims_a_key_for_exactly_one_owner() {
        let pk = [9u8; PUBKEY_BYTES];
        let mut cl = Cluster::new(&pk, NodeClass::Server, |_, _| {}).unwrap();
        let mut t = 0u64;
        for _ in 0..200 {
            t += 20;
            cl.tick(t);
            if cl.is_leader() {
                break;
            }
        }
        assert!(cl.is_leader());
        // claim an absent key (expect empty) → wins.
        cl.cas(b"claim", b"", b"node-1").unwrap();
        for _ in 0..40 {
            t += 20;
            cl.tick(t);
        }
        assert_eq!(cl.get(b"claim").as_deref(), Some(&b"node-1"[..]));
        // a contender expecting "absent" loses (no-op).
        cl.cas(b"claim", b"", b"node-2").unwrap();
        for _ in 0..40 {
            t += 20;
            cl.tick(t);
        }
        assert_eq!(cl.get(b"claim").as_deref(), Some(&b"node-1"[..]));
        // matching expectation hands it off.
        cl.cas(b"claim", b"node-1", b"node-2").unwrap();
        for _ in 0..40 {
            t += 20;
            cl.tick(t);
        }
        assert_eq!(cl.get(b"claim").as_deref(), Some(&b"node-2"[..]));
    }

    #[test]
    fn members_and_classes() {
        let a = [1u8; PUBKEY_BYTES];
        let b = [2u8; PUBKEY_BYTES];
        let mut cl = Cluster::new(&a, NodeClass::Server, |_, _| {}).unwrap();
        // add a laptop learner.
        cl.add_member(&b, NodeClass::Laptop, -1).unwrap();
        assert_eq!(cl.member_count(), 2);
        assert!(cl.is_voter(&a));
        assert!(!cl.is_voter(&b)); /* learner */
        let members = cl.members();
        assert_eq!(members.len(), 2);
        assert!(members.contains(&a) && members.contains(&b));
        // promote the learner → voter.
        cl.promote(&b).unwrap();
        assert!(cl.is_voter(&b));
        // remove it.
        cl.remove(&b).unwrap();
        assert_eq!(cl.member_count(), 1);
    }
}
