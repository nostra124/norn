#include "streammux.h"
#include "stream.h"

#include <stdlib.h>
#include <string.h>

/* one logical stream: a full stream_t plus the id its segments are framed with. */
typedef struct {
    int used;
    uint16_t sid;
    streammux_t *m;          /* back-ref so the per-stream send cb reaches the mux send */
    stream_t *st;
} lstream_t;

struct streammux {
    streammux_send_fn send;
    void *ctx;
    lstream_t s[STREAMMUX_MAX];
};

/* per-stream send: frame the segment with its 2-byte id, then hand to the mux's
 * send (which seals + sends over the channel). */
static int lstream_send(void *vctx, const unsigned char *seg, size_t len) {
    lstream_t *ls = (lstream_t *)vctx;
    /* defence-in-depth: stream.c bounds every emitted segment to STREAM_SEG_MAX, so
     * this never trips — kept to guard the fixed `out` buffer. (Unreachable arm.) */
    if (len > STREAM_SEG_MAX) return -1;   /* LCOV_EXCL_BR_LINE */
    unsigned char out[STREAMMUX_FRAME + STREAM_SEG_MAX];
    out[0] = (unsigned char)(ls->sid >> 8);
    out[1] = (unsigned char)(ls->sid & 0xff);
    memcpy(out + STREAMMUX_FRAME, seg, len);
    return ls->m->send(ls->m->ctx, out, len + STREAMMUX_FRAME);
}

streammux_t *streammux_new(streammux_send_fn send, void *ctx) {
    streammux_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;   /* LCOV_EXCL_BR_LINE: calloc failure not unit-tested */
    m->send = send;
    m->ctx = ctx;
    return m;
}

void streammux_free(streammux_t *m) {
    if (!m) return;
    for (int i = 0; i < STREAMMUX_MAX; i++)
        if (m->s[i].used) stream_free(m->s[i].st);
    free(m);
}

static lstream_t *find(streammux_t *m, uint16_t sid) {
    for (int i = 0; i < STREAMMUX_MAX; i++)
        if (m->s[i].used && m->s[i].sid == sid) return &m->s[i];
    return NULL;
}

static lstream_t *open_stream(streammux_t *m, uint16_t sid) {
    lstream_t *ls = find(m, sid);
    if (ls) return ls;
    for (int i = 0; i < STREAMMUX_MAX; i++) {
        if (m->s[i].used) continue;
        lstream_t *n = &m->s[i];
        n->used = 1; n->sid = sid; n->m = m;
        n->st = stream_new(lstream_send, n);   /* ctx = the lstream, so the cb knows the id */
        if (!n->st) { n->used = 0; return NULL; }   /* LCOV_EXCL_BR_LINE: stream_new calloc failure not unit-tested */
        return n;
    }
    return NULL;                                 /* table full */
}

int streammux_open(streammux_t *m, uint16_t sid) {
    if (!m) return -1;
    return open_stream(m, sid) ? 0 : -1;
}

int streammux_write(streammux_t *m, uint16_t sid, const unsigned char *data, size_t len, uint32_t now_ms) {
    if (!m) return -1;
    lstream_t *ls = open_stream(m, sid);
    if (!ls) return -1;
    return stream_write(ls->st, data, len, now_ms);
}

int streammux_read(streammux_t *m, uint16_t sid, unsigned char *out, size_t cap) {
    if (!m) return 0;
    lstream_t *ls = find(m, sid);
    return ls ? stream_read(ls->st, out, cap) : 0;
}

void streammux_input(streammux_t *m, const unsigned char *seg, size_t len, uint32_t now_ms) {
    if (!m || len < STREAMMUX_FRAME) return;
    uint16_t sid = (uint16_t)((seg[0] << 8) | seg[1]);
    lstream_t *ls = open_stream(m, sid);          /* auto-open peer-initiated streams */
    if (!ls) return;
    stream_input(ls->st, seg + STREAMMUX_FRAME, len - STREAMMUX_FRAME, now_ms);
}

void streammux_tick(streammux_t *m, uint32_t now_ms) {
    if (!m) return;
    for (int i = 0; i < STREAMMUX_MAX; i++)
        if (m->s[i].used) stream_tick(m->s[i].st, now_ms);
}

size_t streammux_readable(const streammux_t *m, uint16_t sid) {
    if (!m) return 0;
    for (int i = 0; i < STREAMMUX_MAX; i++)
        if (m->s[i].used && m->s[i].sid == sid) return stream_readable(m->s[i].st);
    return 0;
}

int streammux_count(const streammux_t *m) {
    int n = 0;
    if (m) for (int i = 0; i < STREAMMUX_MAX; i++) if (m->s[i].used) n++;
    return n;
}

/* const-safe lookup helper for the read-only delegators below. */
static const lstream_t *cfind(const streammux_t *m, uint16_t sid) {
    for (int i = 0; i < STREAMMUX_MAX; i++)
        if (m->s[i].used && m->s[i].sid == sid) return &m->s[i];
    return NULL;
}

void streammux_finish(streammux_t *m, uint16_t sid, uint32_t now_ms) {
    if (!m) return;
    lstream_t *ls = open_stream(m, sid);   /* finishing a not-yet-opened stream opens it */
    if (ls) stream_finish(ls->st, now_ms);
}

int streammux_peer_finished(const streammux_t *m, uint16_t sid) {
    if (!m) return 1;
    const lstream_t *ls = cfind(m, sid);
    return ls ? stream_peer_finished(ls->st) : 1;
}

int streammux_send_done(const streammux_t *m, uint16_t sid) {
    if (!m) return 1;
    const lstream_t *ls = cfind(m, sid);
    return ls ? stream_send_done(ls->st) : 1;
}
