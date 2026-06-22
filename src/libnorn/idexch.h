#ifndef IDEXCH_H
#define IDEXCH_H

#include <stddef.h>

/* Signed identity exchange — a stateless, single-round-trip protocol two bifrost
 * nodes run on their MAIN sockets to learn (and cache) each other's verified
 * identity. Unlike a secure-channel handshake it needs no key agreement and no
 * per-peer state: each side sends one cleartext message authenticated by an
 * ed25519 signature over the asserted fields, which proves the sender holds
 * `pub` and binds it to (account, ula, version). Nothing in the message is
 * secret (a public key, an account name, a ULA), so cleartext is fine.
 *
 *   REQ : requester -> peer's main socket, carries a random nonce.
 *   RESP: peer's main socket -> requester, echoes the nonce (freshness) so the
 *         requester knows the reply is live and bound to its request.
 *
 * The responder is stateless: verify, cache the requester, reply. No fork.
 *
 * An optional length-prefixed payload rides along, also signed: in a `get` REQ
 * it carries the requested KV name; in the RESP it carries the value (empty for
 * a plain identity exchange). This makes pub/get a one-round-trip, authenticated,
 * point-to-point lookup over the same transport.
 *
 * Wire layout (fixed offsets, 2-byte big-endian payload length):
 *   "BFID"(4) type(1) nonce(16) pub(32) ula(16) vlen(1) ver alen(1) acct
 *   plen(2) payload sig(64)
 * The signature covers everything from `type` through `payload` (exclusive). */

#define IDEXCH_MAGIC0 0x42  /* 'B' */
#define IDEXCH_MAGIC1 0x46  /* 'F' */
#define IDEXCH_MAGIC2 0x49  /* 'I' */
#define IDEXCH_MAGIC3 0x44  /* 'D' */

#define IDEXCH_REQ     0x01
#define IDEXCH_RESP    0x02
#define IDEXCH_RDV     0x03  /* A -> R: "rendezvous me with <target>" (payload = target account) */
#define IDEXCH_CONNECT 0x04  /* R -> A/T: "<account> is at <ep>, punch to it"
                             * (payload = account NUL + ip(4) + port(2, BE)) */
#define IDEXCH_RECORD  0x05  /* gossip: push my signed record. k = the idexch pub;
                             * payload = seq(4,BE) + sig(64) + v (BEP-44 record). */
#define IDEXCH_RDV_NID 0x06  /* A -> R: "rendezvous me with the peer whose node-id is
                             * <payload>" (payload = node_id, 20 bytes = SHA256(acct)[:20]).
                             * For peers known ONLY via gossip, where A holds no account
                             * to name the target — the relay resolves the node-id from
                             * its own peerstore and introduces. (BUG-008.) */
#define IDEXCH_RECORD_PRIV 0x07 /* FEAT-048 (0.17): PRIVATE capability gossip — same payload as
                             * IDEXCH_RECORD, but only sent to / accepted from a `trust`ed peer and
                             * NEVER published to the public DHT or re-gossiped to non-trusted peers.
                             * The friend-to-friend channel: e.g. a stealth hub's endpoint reaches its
                             * trusted clients here, never the public layer. */
#define IDEXCH_SVC_PRIV 0x08 /* FEAT-035 20d (0.22): PRIVATE service gossip — payload =
                             * seq(4,BE) + sig(64) + svc-blob (servicestore svc_encode entries),
                             * signed by the sender's idexch key. Sent ONLY to a `trust`ed peer
                             * and accepted ONLY from one, so service NAMES never reach public
                             * gossip or the DHT — the trusted-overlay gate the single public
                             * per-identity record cannot itself provide. */

#define IDEXCH_PAYLOAD_MAX 1024  /* max KV name/value carried in one message */
#define IDEXCH_MAX  1400         /* upper bound on a serialized message */

/* Protocol capability bitmask — advertised in the (signed) RESP payload of a
 * plain identity exchange, after the 1-byte auth flag: [auth][caps:4 big-endian].
 * Old nodes read only the auth byte and ignore the rest, so this is backward
 * compatible. Each bit maps to a BPE; a feature is used only if both peers
 * advertise it (capability negotiation, not version-sniffing). */
#define CAP_VPN        (1u << 0)  /* runs the VPN mesh (TUN) */
#define CAP_RELAY      (1u << 1)  /* can relay for others (reachable) — BPE-0009 */
#define CAP_RENDEZVOUS (1u << 2)  /* BEP-55 rendezvous hole punch — BPE-0005 */
#define CAP_READONLY   (1u << 3)  /* BEP-43 read-only/client-only — BPE-0003 */
#define CAP_KEEPALIVE  (1u << 4)  /* sends periodic peer keepalive pings */
#define CAP_BEP44      (1u << 5)  /* BEP-44 mutable signed records — BPE-0004 (future) */
#define CAP_GOSSIP     (1u << 6)  /* peer-table gossip — BPE-0024 (future) */
#define CAP_ZEROCONF   (1u << 7)  /* mDNS LAN discovery — BPE-0006 (future) */
#define CAP_EXIT       (1u << 8)  /* offers exit-node / subnet routing — FEAT-019 */
#define CAP_UDP        (1u << 9)  /* speaks the UDP transport (always) — FEAT-038 */
#define CAP_TCP        (1u << 10) /* serves a TCP relay hub (root + relay_hub on) — FEAT-038 */
#define CAP_WS         (1u << 11) /* serves a WebSocket transport (path /api/bifrost/v1) — FEAT-047 */
#define CAP_REALITY    (1u << 12) /* serves a VLESS+Reality transport via Xray — FEAT-050 (0.18) */
#define CAP_SHADOWSOCKS (1u << 13) /* serves a Shadowsocks transport via Xray — FEAT-062. Distinct
                                    * from CAP_REALITY because ONE tool (xray) serves several wire
                                    * protocols: the bit names the protocol a dialer must speak, not
                                    * the binary the hub runs. */
#define CAP_XHTTP      (1u << 14) /* serves a CDN-frontable XHTTP carrier via Xray — FEAT-063. Rides a
                                    * CDN edge (the origin IP stays hidden), so it survives IP-blocking
                                    * — the complement to CAP_REALITY (which needs a reachable hub). */

/* True if buf begins with the BFID magic. */
int idexch_is(const unsigned char *buf, size_t len);

/* Serialize + sign a REQ/RESP into out (capacity outcap, >= IDEXCH_MAX is safe).
 * payload may be NULL/0 for a plain identity exchange. Returns the message
 * length, or -1 on bad args / overflow. */
int idexch_build(unsigned char type, const unsigned char nonce[16],
                 const unsigned char pub[32], const unsigned char sk[64],
                 const unsigned char ula[16], const char *version,
                 const char *account, const unsigned char *payload, size_t paylen,
                 unsigned char *out, size_t outcap);

/* Parse and VERIFY the signature against the embedded public key. On success
 * (returns 0) fills every non-NULL out parameter; payload (if non-NULL) receives
 * up to paycap bytes and *paylen the actual length. Returns -1 on a malformed
 * message or a bad signature. */
int idexch_parse(const unsigned char *buf, size_t len,
                 unsigned char *type, unsigned char nonce[16],
                 unsigned char pub[32], unsigned char ula[16],
                 char *version, size_t vcap, char *account, size_t acap,
                 unsigned char *payload, size_t paycap, size_t *paylen);

#endif
