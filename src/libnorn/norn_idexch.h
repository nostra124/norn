/**
 * @file norn_idexch.h
 * @brief Generic identity exchange protocol.
 *
 * A stateless, single-round-trip protocol for two peers to learn each other's
 * verified public key. Unlike a secure-channel handshake, it needs no key
 * agreement and no per-peer state: each side sends one cleartext message
 * authenticated by a signature, proving the sender holds the private key.
 *
 * @par Wire Format
 *
 * The message is a signed assertion binding:
 * - Public key (identity)
 * - Nonce (freshness, echoed back)
 * - Endpoint (IP + port, optional)
 * - Opaque application payload (passthrough)
 *
 * Layout (all multi-byte fields are big-endian):
 * ```
 * [magic:4][type:1][nonce:16][pubkey:N][ep_ip:4][ep_port:2]
 * [paylen:2][payload:M][sig:S]
 * ```
 *
 * Where:
 * - N = suite->pubkey_len
 * - S = suite->sig_len
 * - M <= NORN_IDEXCH_PAYLOAD_MAX
 *
 * The signature covers everything from `type` through `payload` (exclusive of magic and sig).
 *
 * @par Usage
 *
 * Request:
 * @code
 * unsigned char nonce[16];
 * randombytes_buf(nonce, 16);
 *
 * unsigned char req[NORN_IDEXCH_MAX];
 * int len = norn_idexch_build(NORN_IDEXCH_REQ, nonce, pubkey, secret,
 *                             NULL, 0, NULL, 0, req, sizeof(req), suite);
 * // send req to peer
 * @endcode
 *
 * Response:
 * @code
 * unsigned char resp[NORN_IDEXCH_MAX];
 * int len = norn_idexch_build(NORN_IDEXCH_RESP, req_nonce, my_pubkey, my_secret,
 *                             &peer_ip, peer_port, payload, paylen, resp, sizeof(resp), suite);
 * // send resp back to requester
 * @endcode
 *
 * @par Application Payload
 *
 * The payload is opaque to norn — applications define their own schemas:
 * - bifrost: account, ULA, version, capabilities (moved to bifrost layer)
 * - wyrd: Nostr pubkey, relay URLs, trust signals (defined by wyrd)
 * - Custom: Any application-specific data
 *
 * @par Thread Safety
 * All functions are thread-safe.
 */

#ifndef NORN_IDEXCH_H
#define NORN_IDEXCH_H

#include "norn_suite.h"
#include <stdint.h>
#include <stddef.h>

/** @brief Magic bytes for norn identity exchange: "NORN" */
#define NORN_IDEXCH_MAGIC0 0x4E  /* 'N' */
#define NORN_IDEXCH_MAGIC1 0x4F  /* 'O' */
#define NORN_IDEXCH_MAGIC2 0x52  /* 'R' */
#define NORN_IDEXCH_MAGIC3 0x4E  /* 'N' */

/** @brief Request message type */
#define NORN_IDEXCH_REQ  0x01

/** @brief Response message type */
#define NORN_IDEXCH_RESP 0x02

/** @brief Maximum payload size (1024 bytes) */
#define NORN_IDEXCH_PAYLOAD_MAX 1024

/** @brief Maximum message size (conservative upper bound) */
#define NORN_IDEXCH_MAX 1400

/** @brief Nonce size (16 bytes, random) */
#define NORN_IDEXCH_NONCE_LEN 16

/**
 * @brief Check if a buffer contains a norn identity exchange message.
 *
 * @param buf Buffer to check
 * @param len Buffer length
 * @return 1 if it starts with "NORN" magic, 0 otherwise
 */
int norn_idexch_is(const unsigned char *buf, size_t len);

/**
 * @brief Build and sign an identity exchange message.
 *
 * @param type Message type (NORN_IDEXCH_REQ or NORN_IDEXCH_RESP)
 * @param nonce 16-byte nonce (requester generates, responder echoes)
 * @param pubkey Public key (suite->pubkey_len bytes)
 * @param secret Secret key (suite->secret_len bytes)
 * @param endpoint_ip Optional endpoint IP (network byte order, or NULL)
 * @param endpoint_port Optional endpoint port (network byte order, or 0)
 * @param payload Optional application payload (or NULL)
 * @param paylen Payload length (0 if no payload)
 * @param out Output buffer (must have at least NORN_IDEXCH_MAX bytes)
 * @param outcap Output buffer capacity
 * @param suite Crypto suite
 * @return Message length on success, -1 on error
 */
int norn_idexch_build(unsigned char type,
                      const unsigned char nonce[NORN_IDEXCH_NONCE_LEN],
                      const unsigned char pubkey[], const unsigned char secret[],
                      const uint32_t *endpoint_ip, uint16_t endpoint_port,
                      const unsigned char payload[], size_t paylen,
                      unsigned char out[], size_t outcap,
                      const norn_crypto_suite_t *suite);

/**
 * @brief Parse and verify an identity exchange message.
 *
 * @param buf Input buffer
 * @param len Buffer length
 * @param type Output: message type (or NULL)
 * @param nonce Output: 16-byte nonce (or NULL)
 * @param pubkey Output: public key (caller allocates suite->pubkey_len bytes)
 * @param endpoint_ip Output: endpoint IP (or NULL)
 * @param endpoint_port Output: endpoint port (or NULL)
 * @param payload Output: payload (caller allocates, or NULL)
 * @param paycap Payload buffer capacity
 * @param paylen Output: actual payload length (or NULL)
 * @param suite Crypto suite
 * @return 0 on success (signature verified), -1 on error
 */
int norn_idexch_parse(const unsigned char buf[], size_t len,
                      unsigned char *type,
                      unsigned char nonce[NORN_IDEXCH_NONCE_LEN],
                      unsigned char pubkey[],
                      uint32_t *endpoint_ip, uint16_t *endpoint_port,
                      unsigned char payload[], size_t paycap, size_t *paylen,
                      const norn_crypto_suite_t *suite);

#endif /* NORN_IDEXCH_H */