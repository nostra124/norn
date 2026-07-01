/* SPDX-License-Identifier: MIT */
#ifndef NORND_DHT_PERSIST_H
#define NORND_DHT_PERSIST_H

#include "publog.h"

/* Save the DHT store + publog to a binary file. Returns 0 / -1. */
int dht_persist_save(const char *path, publog_t *publog);

/* Load the DHT store + publog from a binary file (restores held records via
 * norn_dht_restore_* and repopulates the publog). Returns 0 / -1. */
int dht_persist_load(const char *path, publog_t *publog);

#endif /* NORND_DHT_PERSIST_H */