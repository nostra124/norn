/**
 * @file client.h
 * @brief `norn cluster …` CLI client helpers (FEAT-030).
 *
 * Pure, testable pieces of the CLI's cluster namespace: turn argv into an IPC
 * request, and turn a response into printable output + a process exit code.
 * The Unix-socket round-trip itself is glue in norn.c.
 */
#ifndef NORND_CLIENT_H
#define NORND_CLIENT_H

#include <stddef.h>
#include "ipc.h"

/**
 * Build a cluster IPC request from `norn cluster` subcommand arguments.
 * @param argc count starting at the subcommand (argv[0] == "put"/"get"/…)
 * @param argv argument vector
 * @param req  filled request on success
 * @param err  optional human error (may be NULL)
 * @return 0 on success, -1 on unknown verb / missing or oversized args.
 */
int nornd_client_build_req(int argc, char **argv, nornd_ipc_req_t *req,
                           char *err, size_t errcap);

/**
 * Format a response for the originating request into `out` (binary-safe via
 * `*outlen`). Returns the process exit code: 0 on success, 1 when the daemon
 * reported `ok=0`.
 */
int nornd_client_format(const nornd_ipc_req_t *req, const nornd_ipc_resp_t *resp,
                        char *out, size_t cap, size_t *outlen);

#endif /* NORND_CLIENT_H */
