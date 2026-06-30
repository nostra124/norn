/* SPDX-License-Identifier: MIT */
/**
 * @file cli_keys.h
 * @brief `norn keys <nodeid>` command glue (FEAT-031).
 */
#ifndef NORND_CLI_KEYS_H
#define NORND_CLI_KEYS_H

/** Resolve and print a peer's SSH + GPG public keys. `argv[0]` is the hex
 *  nodeid. Returns a process exit code. */
int nornd_cli_keys(int argc, char **argv);

#endif /* NORND_CLI_KEYS_H */
