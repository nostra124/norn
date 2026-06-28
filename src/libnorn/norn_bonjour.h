#ifndef NORN_BONJOUR_H
#define NORN_BONJOUR_H

#include <stdint.h>

struct norn_client;

typedef struct norn_bonjour norn_bonjour_t;

norn_bonjour_t *norn_bonjour_new(struct norn_client *client,
                                  uint16_t port,
                                  const unsigned char pubkey[32]);
void norn_bonjour_free(norn_bonjour_t *bj);

#endif /* NORN_BONJOUR_H */
