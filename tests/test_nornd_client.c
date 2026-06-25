/* Unit tests for the norn cluster CLI client helpers (FEAT-030). 100% cov. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "client.h"
#include "dispatch.h"

static void test_build_basic(void) {
    nornd_ipc_req_t r;
    char err[64];
    char *put[] = {"put", "k", "v"};
    assert(nornd_client_build_req(3, put, &r, err, sizeof(err)) == 0);
    assert(strcmp(r.op, "put") == 0 && r.klen == 1 && r.has_val && r.vlen == 1);

    char *get[] = {"get", "k"};
    assert(nornd_client_build_req(2, get, &r, err, sizeof(err)) == 0);
    assert(strcmp(r.op, "get") == 0 && r.klen == 1);

    char *del[] = {"del", "k"};
    assert(nornd_client_build_req(2, del, &r, err, sizeof(err)) == 0);
    assert(strcmp(r.op, "del") == 0);

    char *watch[] = {"watch", "pre"};
    assert(nornd_client_build_req(2, watch, &r, err, sizeof(err)) == 0);
    assert(strcmp(r.op, "watch") == 0 && r.klen == 3);

    char *cas[] = {"cas", "k", "old", "new"};
    assert(nornd_client_build_req(4, cas, &r, err, sizeof(err)) == 0);
    assert(strcmp(r.op, "cas") == 0 && r.has_expect && r.elen == 3 && r.has_val);

    char *members[] = {"members"};
    assert(nornd_client_build_req(1, members, &r, err, sizeof(err)) == 0);
    assert(strcmp(r.op, "members") == 0);
    char *leader[] = {"leader"};
    assert(nornd_client_build_req(1, leader, &r, err, sizeof(err)) == 0);
    char *status[] = {"status"};
    assert(nornd_client_build_req(1, status, &r, err, sizeof(err)) == 0);
    assert(strcmp(r.op, "status") == 0);
}

static void test_build_errors(void) {
    nornd_ipc_req_t r;
    char err[64];
    assert(nornd_client_build_req(0, NULL, &r, err, sizeof(err)) == -1);
    char *put1[] = {"put", "k"};
    assert(nornd_client_build_req(2, put1, &r, err, sizeof(err)) == -1);
    char *get0[] = {"get"};
    assert(nornd_client_build_req(1, get0, &r, err, sizeof(err)) == -1);
    char *cas3[] = {"cas", "k", "e"};
    assert(nornd_client_build_req(3, cas3, &r, err, sizeof(err)) == -1);
    char *bogus[] = {"frobnicate"};
    assert(nornd_client_build_req(1, bogus, &r, err, sizeof(err)) == -1);
    /* err present but cap 0 (set_err must not write) */
    char zcap[8];
    zcap[0] = 'Z';
    assert(nornd_client_build_req(1, bogus, &r, zcap, 0) == -1);
    assert(zcap[0] == 'Z');
    /* tiny cap truncates the message */
    char tiny[4];
    assert(nornd_client_build_req(1, bogus, &r, tiny, sizeof(tiny)) == -1);
    assert(strlen(tiny) == 3);

    /* oversized args. `big` overflows a key (256), `bigval` a value/expect
       (4096). Each covers a distinct operand of the put/cas `||` chains.
       (err == NULL on the first exercises the set_err skip path.) */
    static char big[NORND_IPC_MAX_KEY + 8];
    memset(big, 'x', sizeof(big) - 1);
    static char bigval[NORND_IPC_MAX_VAL + 8];
    memset(bigval, 'y', sizeof(bigval) - 1);

    char *putbigk[] = {"put", big, "v"};
    assert(nornd_client_build_req(3, putbigk, &r, NULL, 0) == -1);
    char *putbigv[] = {"put", "k", bigval};
    assert(nornd_client_build_req(3, putbigv, &r, err, sizeof(err)) == -1);
    char *getbig[] = {"get", big};
    assert(nornd_client_build_req(2, getbig, &r, err, sizeof(err)) == -1);
    char *casbigk[] = {"cas", big, "e", "v"};
    assert(nornd_client_build_req(4, casbigk, &r, err, sizeof(err)) == -1);
    char *casbige[] = {"cas", "k", bigval, "v"};
    assert(nornd_client_build_req(4, casbige, &r, err, sizeof(err)) == -1);
    char *casbigv[] = {"cas", "k", "e", bigval};
    assert(nornd_client_build_req(4, casbigv, &r, err, sizeof(err)) == -1);
}

static nornd_ipc_req_t req_op(const char *op) {
    nornd_ipc_req_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.op, op);
    return r;
}

static void test_format(void) {
    char out[1024];
    size_t n;
    nornd_ipc_resp_t resp;

    /* error response */
    memset(&resp, 0, sizeof(resp));
    resp.ok = 0;
    resp.has_err = 1;
    strcpy(resp.err, "no leader");
    nornd_ipc_req_t rq = req_op("put");
    assert(nornd_client_format(&rq, &resp, out, sizeof(out), &n) == 1);
    assert(memcmp(out, "error: no leader\n", n) == 0);

    /* error response without err string */
    memset(&resp, 0, sizeof(resp));
    resp.ok = 0;
    assert(nornd_client_format(&rq, &resp, out, sizeof(out), &n) == 1);
    assert(strncmp(out, "error: request failed", 21) == 0);

    /* get with value */
    memset(&resp, 0, sizeof(resp));
    resp.ok = 1;
    resp.has_val = 1;
    memcpy(resp.val, "hi", 2);
    resp.vlen = 2;
    rq = req_op("get");
    assert(nornd_client_format(&rq, &resp, out, sizeof(out), &n) == 0);
    assert(n == 3 && memcmp(out, "hi\n", 3) == 0);

    /* get without value (absent has_val) */
    memset(&resp, 0, sizeof(resp));
    resp.ok = 1;
    assert(nornd_client_format(&rq, &resp, out, sizeof(out), &n) == 0);
    assert(n == 1 && out[0] == '\n');

    /* leader → hex */
    memset(&resp, 0, sizeof(resp));
    resp.ok = 1;
    resp.has_val = 1;
    resp.val[0] = 0xde;
    resp.val[1] = 0xad;
    resp.vlen = 2;
    rq = req_op("leader");
    assert(nornd_client_format(&rq, &resp, out, sizeof(out), &n) == 0);
    assert(memcmp(out, "dead\n", 5) == 0);

    /* members → one hex line per item */
    memset(&resp, 0, sizeof(resp));
    resp.ok = 1;
    resp.n_items = 2;
    resp.items[0].len = 1;
    resp.items[0].data[0] = 0x01;
    resp.items[1].len = 1;
    resp.items[1].data[0] = 0xff;
    rq = req_op("members");
    assert(nornd_client_format(&rq, &resp, out, sizeof(out), &n) == 0);
    assert(memcmp(out, "01\nff\n", 6) == 0);

    /* status: leader role with leader pubkey + members */
    memset(&resp, 0, sizeof(resp));
    resp.ok = 1;
    resp.has_val = 1;
    resp.val[0] = 1;
    resp.val[1] = 0xab;
    resp.vlen = 2;
    resp.n_items = 3;
    rq = req_op("status");
    assert(nornd_client_format(&rq, &resp, out, sizeof(out), &n) == 0);
    out[n] = '\0';
    assert(strstr(out, "role: leader\n") && strstr(out, "leader: ab\n") &&
           strstr(out, "members: 3\n"));

    /* status: follower, no leader pubkey (vlen == 1) */
    memset(&resp, 0, sizeof(resp));
    resp.ok = 1;
    resp.has_val = 1;
    resp.val[0] = 0;
    resp.vlen = 1;
    assert(nornd_client_format(&rq, &resp, out, sizeof(out), &n) == 0);
    out[n] = '\0';
    assert(strstr(out, "role: follower\n") && !strstr(out, "leader:") &&
           strstr(out, "members: 0\n"));

    /* status: vlen == 0 → follower branch via short-circuit */
    memset(&resp, 0, sizeof(resp));
    resp.ok = 1;
    assert(nornd_client_format(&rq, &resp, out, sizeof(out), &n) == 0);
    out[n] = '\0';
    assert(strstr(out, "role: follower\n"));

    /* put/del/cas/watch ack */
    memset(&resp, 0, sizeof(resp));
    resp.ok = 1;
    rq = req_op("put");
    assert(nornd_client_format(&rq, &resp, out, sizeof(out), &n) == 0);
    assert(memcmp(out, "OK\n", 3) == 0);
}

int main(void) {
    test_build_basic();
    test_build_errors();
    test_format();
    printf("all nornd client tests passed\n");
    return 0;
}
