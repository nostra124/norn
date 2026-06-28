/**
 * @file cli_node.h
 * @brief `norn node …` — fetch content directly from a peer (FEAT-033).
 *
 * Dials a peer over a norn session and opens a NORN_SVC_SERVED_KV stream to run
 * one served-KV request, printing the body to stdout:
 *   norn node <64-hex-pubkey>[@host:port] get  <key>
 *   norn node <64-hex-pubkey>[@host:port] cat  <sha256-hex>
 *   norn node <64-hex-pubkey>[@host:port] list [prefix]
 * Without @host:port the peer endpoint is resolved via the DHT. Socket/dial glue
 * — excluded from unit coverage; exercised by the served-KV PIT.
 */
#ifndef NORND_CLI_NODE_H
#define NORND_CLI_NODE_H

/* argv: [0]=<pubkey[@host:port]>, [1]=<get|cat|list>, [2]=<arg> (opt for list).
 * Returns 0 on success, 1 on error, 2 on usage error. */
int nornd_cli_node(int argc, char **argv);

#endif /* NORND_CLI_NODE_H */
