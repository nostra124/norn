/* norn_transaction.c — Transaction tracking for async operations */
#include "norn_transaction.h"
#include <string.h>

void norn_transaction_init(norn_transaction_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->next_id = 1;
}

norn_transaction_t *norn_transaction_new(norn_transaction_state_t *state,
                                         norn_transaction_type_t type,
                                         const unsigned char *target) {
    if (!state || state->count >= NORN_MAX_TRANSACTIONS) return NULL;
    
    norn_transaction_t *txn = &state->transactions[state->count];
    txn->id = norn_transaction_next_id(state);
    txn->type = type;
    txn->created = time(NULL);
    if (target) {
        memcpy(txn->target, target, sizeof(txn->target));
    }
    txn->get_callback = NULL;
    txn->peer_callback = NULL;
    txn->user_data = NULL;
    txn->completed = 0;
    
    state->count++;
    return txn;
}

norn_transaction_t *norn_transaction_find(norn_transaction_state_t *state,
                                          uint32_t id) {
    if (!state) return NULL;
    
    for (int i = 0; i < state->count; i++) {
        if (state->transactions[i].id == id) {
            return &state->transactions[i];
        }
    }
    return NULL;
}

void norn_transaction_remove(norn_transaction_state_t *state,
                             norn_transaction_t *txn) {
    if (!state || !txn) return;
    
    int idx = txn - state->transactions;
    if (idx < 0 || idx >= state->count) return;
    
    /* Swap with last and decrement */
    state->transactions[idx] = state->transactions[state->count - 1];
    state->count--;
}

int norn_transaction_expire(norn_transaction_state_t *state,
                            int max_age_secs) {
    if (!state) return 0;
    
    time_t now = time(NULL);
    int expired = 0;
    int write_idx = 0;
    
    for (int read_idx = 0; read_idx < state->count; read_idx++) {
        if (now - state->transactions[read_idx].created > max_age_secs) {
            expired++;
        } else {
            if (write_idx != read_idx) {
                state->transactions[write_idx] = state->transactions[read_idx];
            }
            write_idx++;
        }
    }
    
    state->count = write_idx;
    return expired;
}

uint32_t norn_transaction_next_id(norn_transaction_state_t *state) {
    if (!state) return 0;
    uint32_t id = state->next_id++;
    if (state->next_id == 0) state->next_id = 1;  /* Avoid 0 */
    return id;
}