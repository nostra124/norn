/* SPDX-License-Identifier: MIT */
#ifndef NORN_SHA1_H
#define NORN_SHA1_H

#include <stddef.h>
#include <stdint.h>

/* Minimal SHA-1, only for BEP-44 mutable-item targets (target = SHA1(k[+salt])).
 * NOT used for any security-sensitive hashing — identity/keys use ed25519 and
 * SHA-256 (crypto.c). Public-domain implementation (Steve Reid, et al.). */
void sha1(const unsigned char *data, size_t len, unsigned char out[20]);

#endif
