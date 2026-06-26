/* Unit tests for nornd peer transport helpers (FEAT-029 multi-node). 100% cov. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "peers.h"

static const char HEX64[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static void test_parse_ok(void) {
    nornd_peer_t p;

    /* pubkey only -> DHT resolution */
    assert(nornd_peer_parse(HEX64, &p) == 0);
    assert(!p.direct && p.pubkey[0] == 0x01 && p.pubkey[1] == 0x23 &&
           p.pubkey[31] == 0xef);

    /* uppercase hex, still DHT */
    assert(nornd_peer_parse(
               "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
               &p) == 0);
    assert(!p.direct && p.pubkey[0] == 0x01 && p.pubkey[31] == 0xef);

    /* direct endpoint */
    char spec[400];
    snprintf(spec, sizeof(spec), "%s@10.0.0.5:6881", HEX64);
    assert(nornd_peer_parse(spec, &p) == 0);
    assert(p.direct && strcmp(p.host, "10.0.0.5") == 0 && p.port == 6881);

    /* IPv6 literal: host keeps its internal colons (split on the last ':') */
    snprintf(spec, sizeof(spec), "%s@fe80::1:9000", HEX64);
    assert(nornd_peer_parse(spec, &p) == 0);
    assert(p.direct && strcmp(p.host, "fe80::1") == 0 && p.port == 9000);

    /* max port */
    snprintf(spec, sizeof(spec), "%s@h:65535", HEX64);
    assert(nornd_peer_parse(spec, &p) == 0 && p.port == 65535);
}

static void test_parse_errors(void) {
    nornd_peer_t p;
    char spec[400];

    /* null arguments */
    assert(nornd_peer_parse(NULL, &p) == -1);
    assert(nornd_peer_parse(HEX64, NULL) == -1);

    /* wrong pubkey length (no '@', and with '@') */
    assert(nornd_peer_parse("dead", &p) == -1);
    snprintf(spec, sizeof(spec), "%s0", HEX64); /* 65 chars */
    assert(nornd_peer_parse(spec, &p) == -1);
    assert(nornd_peer_parse("dead@h:1", &p) == -1); /* short hex before '@' */

    /* right length but a non-hex digit (high then low nibble) */
    snprintf(spec, sizeof(spec), "z%s", HEX64 + 1);
    assert(nornd_peer_parse(spec, &p) == -1);
    snprintf(spec, sizeof(spec), "0z%s", HEX64 + 2);
    assert(nornd_peer_parse(spec, &p) == -1);
    /* non-hex chars spanning the hexval ranges: '!' (< '0') and ':' ('9'<c<'A') */
    snprintf(spec, sizeof(spec), "!%s", HEX64 + 1);
    assert(nornd_peer_parse(spec, &p) == -1);
    snprintf(spec, sizeof(spec), ":%s", HEX64 + 1);
    assert(nornd_peer_parse(spec, &p) == -1);

    /* endpoint errors */
    snprintf(spec, sizeof(spec), "%s@hostonly", HEX64); /* no ':' */
    assert(nornd_peer_parse(spec, &p) == -1);
    snprintf(spec, sizeof(spec), "%s@:6881", HEX64); /* empty host */
    assert(nornd_peer_parse(spec, &p) == -1);
    snprintf(spec, sizeof(spec), "%s@h:", HEX64); /* empty port */
    assert(nornd_peer_parse(spec, &p) == -1);
    snprintf(spec, sizeof(spec), "%s@h:80a", HEX64); /* non-digit port (> '9') */
    assert(nornd_peer_parse(spec, &p) == -1);
    snprintf(spec, sizeof(spec), "%s@h:8/0", HEX64); /* port char < '0' */
    assert(nornd_peer_parse(spec, &p) == -1);
    snprintf(spec, sizeof(spec), "%s@h:70000", HEX64); /* overflow */
    assert(nornd_peer_parse(spec, &p) == -1);
    snprintf(spec, sizeof(spec), "%s@h:0", HEX64); /* zero */
    assert(nornd_peer_parse(spec, &p) == -1);

    /* host too long for the field */
    char big[400];
    int n = snprintf(big, sizeof(big), "%s@", HEX64);
    memset(big + n, 'h', NORND_PEER_HOST_MAX + 4);
    strcpy(big + n + NORND_PEER_HOST_MAX + 4, ":1");
    assert(nornd_peer_parse(big, &p) == -1);
}

static void test_frame_encode(void) {
    unsigned char out[64];
    unsigned char pay[5] = {1, 2, 3, 4, 5};
    int n = nornd_frame_encode(pay, 5, out, sizeof(out));
    assert(n == 9);
    assert(out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 5);
    assert(memcmp(out + 4, pay, 5) == 0);

    /* errors: null payload, null out, too big, won't fit */
    assert(nornd_frame_encode(NULL, 5, out, sizeof(out)) == -1);
    assert(nornd_frame_encode(pay, 5, NULL, sizeof(out)) == -1);
    assert(nornd_frame_encode(pay, NORND_FRAME_MAX + 1, out, sizeof(out)) == -1);
    assert(nornd_frame_encode(pay, 5, out, 8) == -1); /* needs 9 */
}

static void test_framer_basic(void) {
    nornd_framer_t f;
    nornd_framer_reset(&f);
    const unsigned char *p;
    size_t plen;

    /* empty → need more */
    assert(nornd_framer_next(&f, &p, &plen) == 0);

    /* one frame delivered in two pushes (header, then body in pieces) */
    unsigned char frame[9];
    nornd_frame_encode((const unsigned char *)"hello", 5, frame, sizeof(frame));
    assert(nornd_framer_push(&f, frame, 2) == 0);          /* partial header */
    assert(nornd_framer_next(&f, &p, &plen) == 0);         /* <4 bytes */
    assert(nornd_framer_push(&f, frame + 2, 4) == 0);      /* rest of hdr + part */
    assert(nornd_framer_next(&f, &p, &plen) == 0);         /* body incomplete */
    assert(nornd_framer_push(&f, frame + 6, 3) == 0);      /* finish body */
    assert(nornd_framer_next(&f, &p, &plen) == 1);
    assert(plen == 5 && memcmp(p, "hello", 5) == 0);
    assert(nornd_framer_next(&f, &p, &plen) == 0);         /* drained */

    /* two frames in a single push */
    unsigned char two[18];
    nornd_frame_encode((const unsigned char *)"aaaaa", 5, two, 9);
    nornd_frame_encode((const unsigned char *)"bbbbb", 5, two + 9, 9);
    nornd_framer_reset(&f);
    assert(nornd_framer_push(&f, two, sizeof(two)) == 0);
    assert(nornd_framer_next(&f, &p, &plen) == 1 && memcmp(p, "aaaaa", 5) == 0);
    assert(nornd_framer_next(&f, &p, &plen) == 1 && memcmp(p, "bbbbb", 5) == 0);
    assert(nornd_framer_next(&f, &p, &plen) == 0);
}

static void test_framer_overflow_and_oversize(void) {
    nornd_framer_t f;
    nornd_framer_reset(&f);
    const unsigned char *p;
    size_t plen;

    /* push larger than the whole buffer → -1 and reset */
    static unsigned char huge[NORND_FRAMER_CAP + 8];
    memset(huge, 7, sizeof(huge));
    assert(nornd_framer_push(&f, huge, sizeof(huge)) == -1);
    assert(f.start == 0 && f.end == 0);

    /* a frame header declaring more than NORND_FRAME_MAX → protocol error */
    unsigned char bad[4] = {0xff, 0xff, 0xff, 0xff};
    assert(nornd_framer_push(&f, bad, 4) == 0);
    assert(nornd_framer_next(&f, &p, &plen) == -1);
}

static void test_framer_compaction(void) {
    nornd_framer_t f;
    nornd_framer_reset(&f);
    const unsigned char *p;
    size_t plen;

    /* Fill most of the buffer with one big frame plus 2 trailing bytes, drain
     * the big frame, then push enough that it only fits after compacting the
     * 2 unread bytes to the front (exercises the memmove path). */
    size_t body = NORND_FRAME_MAX - 8; /* framed = body + 4, leaves room */
    static unsigned char big[NORND_FRAMER_CAP];           /* zeroed (static) */
    big[0] = (unsigned char)(body >> 24);
    big[1] = (unsigned char)(body >> 16);
    big[2] = (unsigned char)(body >> 8);
    big[3] = (unsigned char)body;
    assert(nornd_framer_push(&f, big, 4 + body) == 0);
    unsigned char tail[2] = {0, 0};
    assert(nornd_framer_push(&f, tail, 2) == 0);
    assert(nornd_framer_next(&f, &p, &plen) == 1 && plen == body); /* drain big */
    /* now start>0, end-start==2; push enough to force compaction */
    static unsigned char more[64];
    memset(more, 9, sizeof(more));
    assert(nornd_framer_push(&f, more, sizeof(more)) == 0);
    assert(f.start == 0); /* compacted to front */
    assert(f.end == 2 + sizeof(more));
}

int main(void) {
    test_parse_ok();
    test_parse_errors();
    test_frame_encode();
    test_framer_basic();
    test_framer_overflow_and_oversize();
    test_framer_compaction();
    printf("all nornd peers tests passed\n");
    return 0;
}
