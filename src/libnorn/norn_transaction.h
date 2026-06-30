/* SPDX-License-Identifier: MIT */
/* norn_transaction.h — Transaction tracking for async operations */
#ifndef NORN_TRANSACTION_H
#define NORN_TRANSACTION_H

#include <stdint.h>
#include <time.h>
#include "norn.h"
#include "norn_suite.h"
#include "norn_session.h"

#define NORN_MAX_TRANSACTIONS 256
#define NORN_TRANSACTION_TIMEOUT 30

typedef enum {
    TXN_GET_MUTABLE,
    TXN_GET_IMMUTABLE,
    TXN_DISCOVER,
    TXN_ANNOUNCE,
    TXN_FIND_NODE,
    TXN_RESOLVE_ENDPOINT,
    TXN_ANNOUNCE_ENDPOINT
} norn_transaction_type_t;

typedef struct {
    uint32_t id;
    norn_transaction_type_t type;
    time_t created;
    unsigned char target[32];
    norn_get_callback_t get_callback;
    norn_peer_callback_t peer_callback;
    void *user_data;
    int completed;
    /* FEAT-017: Endpoint resolution */
    norn_resolve_callback_t resolve_callback;
    const norn_crypto_suite_t *suite;
} norn_transaction_t;

typedef struct {
    norn_transaction_t transactions[NORN_MAX_TRANSACTIONS];
    int count;
    uint32_t next_id;
} norn_transaction_state_t;

/* Initialize transaction state */
void norn_transaction_init(norn_transaction_state_t *state);

/* Create a new transaction */
norn_transaction_t *norn_transaction_new(norn_transaction_state_t *state,
                                         norn_transaction_type_t type,
                                         const unsigned char *target);

/* Find transaction by ID */
norn_transaction_t *norn_transaction_find(norn_transaction_state_t *state,
                                          uint32_t id);

/* Remove a transaction */
void norn_transaction_remove(norn_transaction_state_t *state,
                             norn_transaction_t *txn);

/* Expire old transactions */
int norn_transaction_expire(norn_transaction_state_t *state,
                            int max_age_secs);

/* Generate transaction ID */
uint32_t norn_transaction_next_id(norn_transaction_state_t *state);

#endif