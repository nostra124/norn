/* SPDX-License-Identifier: MIT */
/**
 * @file cli_cluster.h
 * @brief `norn cluster …` command glue (FEAT-030).
 *
 * Socket round-trip to nornd. The pure request/response logic lives in
 * client.c; this is the I/O wrapper the `norn` CLI calls.
 */
#ifndef NORND_CLI_CLUSTER_H
#define NORND_CLI_CLUSTER_H

/** Run a `norn cluster <sub> …` command. `argc`/`argv` start at the subcommand
 *  (argv[0] == "put"/"get"/…). Returns a process exit code. */
int nornd_cli_cluster(int argc, char **argv);

#endif /* NORND_CLI_CLUSTER_H */
