/* SPDX-License-Identifier: MIT */
/**
 * @file agent.h
 * @brief ssh-agent signing client for nornd identity (FEAT-028).
 *
 * nornd's node identity is the user's ed25519 SSH key. When that key lives in
 * ssh-agent (never on disk in this process), handshakes are authenticated by
 * asking the agent to sign — norn's handshake signs the ephemeral transcript
 * with the static identity, so an agent-backed signer plugs straight into the
 * libnorn signer hook (norn_set_signer) and the raw key never leaves the agent.
 *
 * This module is the ssh-agent wire protocol (RFC draft / OpenSSH PROTOCOL.agent):
 * an SSH_AGENTC_SIGN_REQUEST for an ed25519 key, and parsing the
 * SSH_AGENT_SIGN_RESPONSE back into the raw 64-byte signature. The framing and
 * the request/response round-trip are pure given a byte transport, so they are
 * unit-tested against an in-memory fake agent; the real AF_UNIX socket to
 * $SSH_AUTH_SOCK is thin glue.
 */
#ifndef NORND_AGENT_H
#define NORND_AGENT_H

#include <stddef.h>
#include <stdint.h>

#define NORND_AGENT_SIGBYTES 64   /* ed25519 signature                       */
#define NORND_AGENT_REQ_MAX  4096 /* largest sign request we build           */
#define NORND_AGENT_RESP_MAX 4096 /* largest response body we accept         */

/**
 * Byte transport to the agent socket. `write_all`/`read_all` move exactly `len`
 * bytes (looping over short writes/reads) and return 0 on success, -1 on error
 * or EOF. Injected so the round-trip is testable against an in-memory fake.
 */
typedef struct {
    void *ctx;
    int (*write_all)(void *ctx, const unsigned char *buf, size_t len);
    int (*read_all)(void *ctx, unsigned char *buf, size_t len);
} nornd_agent_io_t;

/**
 * Encode an SSH_AGENTC_SIGN_REQUEST asking the agent to sign `data` with the
 * ed25519 key whose raw public key is `pub`. Writes the full on-wire message
 * (4-byte big-endian length prefix + body) into `out`.
 * @return total message length, or -1 (bad args / won't fit).
 */
int nornd_agent_encode_sign_request(const unsigned char pub[32],
                                    const unsigned char *data, size_t dlen,
                                    unsigned char *out, size_t cap, size_t *outlen);

/**
 * Decode an SSH_AGENT_SIGN_RESPONSE body (the message bytes after the 4-byte
 * frame length) and extract the raw 64-byte ed25519 signature into `sig`.
 * @return 0 on success, -1 on a malformed / non-ed25519 / error response.
 */
int nornd_agent_decode_sign_response(const unsigned char *body, size_t blen,
                                     unsigned char sig[NORND_AGENT_SIGBYTES]);

/**
 * Ask the agent reachable over `io` to sign `data` with ed25519 key `pub`.
 * Encodes the request, writes it, reads the framed response, and decodes the
 * signature into `sig`.
 * @return 0 on success, -1 on any transport or protocol failure.
 */
int nornd_agent_sign_io(const nornd_agent_io_t *io, const unsigned char pub[32],
                        const unsigned char *data, size_t dlen,
                        unsigned char sig[NORND_AGENT_SIGBYTES]);

#endif /* NORND_AGENT_H */
