/* SPDX-License-Identifier: MIT */
#ifndef NORND_PEER_FETCH_H
#define NORND_PEER_FETCH_H

#include <stddef.h>
#include "norn.h"

int nornd_peer_fetch(norn_client_t *client,
                     const char *spec,
                     const char *verb,
                     const char *arg,
                     unsigned char *out, size_t *outlen,
                     char *err, size_t errcap);

#endif
