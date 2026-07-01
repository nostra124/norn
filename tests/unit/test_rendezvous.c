/* SPDX-License-Identifier: MIT */
/**
 * @file test_rendezvous.c
 * @brief Test rendezvous coordination (FEAT-017 Phase 3).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "norn_rendezvous.h"
#include "norn_nat.h"

int main(void) {
    norn_rendezvous_t rv;
    int rc;
    
    printf("Testing rendezvous initialization...\n");
    rc = norn_rendezvous_init(&rv);
    assert(rc == 0);
    assert(rv.pending != NULL);
    assert(rv.pending_count == 0);
    assert(rv.pending_cap == 16);
    
    printf("Testing rendezvous cleanup...\n");
    norn_rendezvous_cleanup(&rv);
    assert(rv.pending == NULL);
    
    printf("Testing NULL handling...\n");
    assert(norn_rendezvous_init(NULL) == -1);
    norn_rendezvous_cleanup(NULL);
    
    printf("Testing rendezvous handle request with NULL...\n");
    norn_holepunch_req_t req;
    norn_holepunch_resp_t resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    
    assert(norn_rendezvous_handle_req(NULL, &req, 0, 0, NULL, &resp) == -1);
    
    norn_rendezvous_init(&rv);
    assert(norn_rendezvous_handle_req(&rv, NULL, 0, 0, NULL, &resp) == -1);
    norn_rendezvous_cleanup(&rv);
    
    printf("All rendezvous tests passed!\n");
    return 0;
}