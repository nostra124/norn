/**
 * @file identity.h
 * @brief SSH-key node identity for nornd (FEAT-028).
 *
 * nornd's node identity is the user's (or host's) Ed25519 SSH key. This module
 * loads it from an OpenSSH private-key file (parse → libsodium keypair) and
 * parses SSH public-key lines (`ssh-ed25519 AAAA… comment`). An OpenSSH ed25519
 * private key's 64-byte secret is exactly libsodium's secret key (seed‖pubkey),
 * so the parsed key signs norn handshakes directly.
 *
 * The parsers are pure (operate on in-memory bytes). ssh-agent signing (which
 * never exposes the key) is a separate signer.
 */
#ifndef NORND_IDENTITY_H
#define NORND_IDENTITY_H

#include <stddef.h>
#include "crypto.h" /* keypair_t */

/**
 * Parse an **unencrypted** OpenSSH ed25519 private key into `out`.
 * @param pem   the key text (`-----BEGIN OPENSSH PRIVATE KEY-----` …)
 * @param pemlen length of `pem`
 * @param out   filled keypair (public_key[32], secret_key[64])
 * @param err   optional buffer for a human error message (may be NULL)
 * @param errcap capacity of `err`
 * @return 0 on success; -1 on malformed/encrypted/unsupported key.
 */
int nornd_identity_parse_openssh(const unsigned char *pem, size_t pemlen,
                                 keypair_t *out, char *err, size_t errcap);

/**
 * Parse an SSH public-key line (`ssh-ed25519 <base64> [comment]`) into the
 * 32-byte raw Ed25519 public key.
 * @return 0 on success, -1 on malformed / non-ed25519.
 */
int nornd_identity_parse_pubkey_line(const char *line, size_t len,
                                     unsigned char pub[32]);

/**
 * Load identity from an OpenSSH private-key file path (reads the file, then
 * nornd_identity_parse_openssh). Glue over the pure parser.
 * @return 0 on success, -1 on error (unreadable / parse failure).
 */
int nornd_identity_load_file(const char *path, keypair_t *out,
                             char *err, size_t errcap);

#endif /* NORND_IDENTITY_H */
