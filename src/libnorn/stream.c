#include "stream.h"
#include <stdlib.h>
#include <string.h>

#define STREAM_WIN_MAX 256          /* hard cap on in-flight segments (buffer size) */
#define STREAM_IW 10                /* initial congestion window (segments, RFC 6928) */
#define STREAM_SSTHRESH_MIN 2       /* floor for the slow-start threshold */
#define STREAM_RTO_INIT 300         /* initial retransmit timeout (until first RTT sample) */
#define STREAM_RTO_MIN 50           /* clamp: floor */
#define STREAM_RTO_MAX 60000        /* clamp: ceiling */
#define STREAM_DUPACK_THRESH 3      /* dup-ACKs before a fast retransmit */
#define STREAM_SACK_BYTES 8         /* 64-bit selective-ack bitmap */
#define PEND_CAP (STREAM_WIN_MAX * STREAM_SEG_PAYLOAD)
#define DELIV_CAP (STREAM_WIN_MAX * STREAM_SEG_PAYLOAD)

#define F_DATA 1
#define F_ACK  2
#define F_FIN  4
#define F_SACK 8                    /* segment carries a SACK bitmap after the header */

typedef struct {
    uint32_t seq;
    uint32_t sent_at;                /* time the seg was (last) put on the wire */
    uint16_t len;
    uint8_t flags;
    uint8_t present;
    uint8_t retx;                    /* retransmitted at least once → no RTT sample (Karn) */
    uint8_t sacked;                  /* peer reported it received (selective ack) */
    unsigned char data[STREAM_SEG_PAYLOAD];
} seg_t;

struct stream {
    stream_send_fn send;
    void *ctx;

    /* send side */
    seg_t sbuf[STREAM_WIN_MAX];          /* in-flight, indexed by seq % WIN */
    uint32_t snd_una;                /* lowest unacked seq */
    uint32_t snd_nxt;                /* next seq to assign */
    uint32_t rto_deadline;           /* 0 = no timer running */
    int fin_queued;                  /* finish() called, FIN not yet segmented */
    int fin_segmented;               /* FIN has been assigned a seq */

    /* RTT estimation (Jacobson/Karn) — all in ms */
    uint32_t srtt;                   /* smoothed RTT (0 = no sample yet) */
    uint32_t rttvar;                 /* RTT variance */
    uint32_t rto;                    /* current retransmit timeout */
    int dupacks;                     /* consecutive dup-ACKs at snd_una */

    /* congestion control (AIMD, FEAT-073) — in segments */
    uint32_t cwnd;                   /* congestion window: max in-flight */
    uint32_t ssthresh;               /* slow-start threshold */
    uint32_t cwnd_cnt;              /* ACKed-segment accumulator for additive increase */
    uint32_t peer_rwnd;              /* peer's advertised receive window (segments, FEAT-073) */

    unsigned char pend[PEND_CAP];    /* app bytes not yet segmented (ring) */
    size_t pend_head, pend_count;

    /* receive side */
    seg_t rbuf[STREAM_WIN_MAX];          /* reassembly, indexed by seq % WIN */
    uint32_t rcv_nxt;                /* next in-order seq expected */
    int peer_fin;                    /* FIN delivered in order */

    unsigned char deliv[DELIV_CAP];  /* delivered bytes for stream_read (ring) */
    size_t deliv_head, deliv_count;
};

static void put32(unsigned char *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff; p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;  p[3] = v & 0xff;
}
static uint32_t get32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static void put16(unsigned char *p, uint16_t v) { p[0] = (v >> 8) & 0xff; p[1] = v & 0xff; }
static uint16_t get16(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void put64(unsigned char *p, uint64_t v) {
    put32(p, (uint32_t)(v >> 32)); put32(p + 4, (uint32_t)(v & 0xffffffffu));
}
static uint64_t get64(const unsigned char *p) {
    return ((uint64_t)get32(p) << 32) | (uint64_t)get32(p + 4);
}

stream_t *stream_new(stream_send_fn send, void *ctx) {
    stream_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;   /* LCOV_EXCL_BR_LINE: calloc failure not unit-tested */
    s->send = send;
    s->ctx = ctx;
    s->rto = STREAM_RTO_INIT;        /* until the first RTT sample */
    s->cwnd = STREAM_IW;             /* slow start from the initial window */
    s->ssthresh = STREAM_WIN_MAX;    /* high → start in slow start */
    s->peer_rwnd = STREAM_WIN_MAX;   /* assume full until the peer advertises otherwise */
    return s;
}

void stream_free(stream_t *s) { free(s); }

/* fold one RTT sample into SRTT/RTTVAR and recompute the RTO (RFC 6298 constants,
 * fixed-point in ms). Never called for an ambiguous (retransmitted) segment (Karn). */
static void rtt_update(stream_t *s, uint32_t r) {
    if (s->srtt == 0) {                          /* first sample */
        s->srtt = r ? r : 1;
        s->rttvar = (r ? r : 1) / 2;
    } else {
        uint32_t d = s->srtt > r ? s->srtt - r : r - s->srtt;
        s->rttvar = (3 * s->rttvar + d) / 4;     /* 3/4 var + 1/4 |srtt-r| */
        s->srtt = (7 * s->srtt + r) / 8;         /* 7/8 srtt + 1/8 r */
    }
    uint32_t rto = s->srtt + 4 * s->rttvar;
    if (rto < STREAM_RTO_MIN) rto = STREAM_RTO_MIN;
    if (rto > STREAM_RTO_MAX) rto = STREAM_RTO_MAX;
    s->rto = rto;
}

/* build the SACK bitmap of out-of-order segments held above rcv_nxt: bit i set ==
 * seq (rcv_nxt + 1 + i) is present. Returns bytes written (0 = no holes, omit). */
/* our advertised receive window: free delivery-buffer space, in segments. */
static uint16_t local_rwnd(const stream_t *s) {
    size_t segs = (DELIV_CAP - s->deliv_count) / STREAM_SEG_PAYLOAD;
    /* DELIV_CAP == STREAM_WIN_MAX * STREAM_SEG_PAYLOAD, so segs is already ≤ WIN_MAX;
     * the clamp is defence-in-depth and never trips. */
    if (segs > STREAM_WIN_MAX) segs = STREAM_WIN_MAX;   /* LCOV_EXCL_BR_LINE */
    return (uint16_t)segs;
}
static size_t write_sack(const stream_t *s, unsigned char *out) {
    uint64_t bm = 0;
    for (int i = 0; i < 63; i++) {
        uint32_t q = s->rcv_nxt + 1 + (uint32_t)i;
        const seg_t *r = &s->rbuf[q % STREAM_WIN_MAX];
        if (!r->present) continue;
        if (r->seq == q) bm |= (uint64_t)1 << i;   /* LCOV_EXCL_BR_LINE: present ⇒ seq==q in-window */
    }
    if (!bm) return 0;
    put64(out, bm);
    return STREAM_SACK_BYTES;
}

/* serialize a data/fin segment with the current cumulative ack and emit it */
static void emit_seg(stream_t *s, const seg_t *seg) {
    unsigned char out[STREAM_SEG_MAX];
    out[0] = seg->flags | F_ACK;
    put32(out + 1, seg->seq);
    put32(out + 5, s->rcv_nxt);
    put16(out + 9, local_rwnd(s));               /* FEAT-073 flow control */
    if (seg->len) memcpy(out + STREAM_HEADER, seg->data, seg->len);
    s->send(s->ctx, out, STREAM_HEADER + seg->len);
}

/* pure cumulative ack, with the SACK bitmap appended when there are holes */
static void emit_ack(stream_t *s) {
    unsigned char out[STREAM_HEADER + STREAM_SACK_BYTES];
    out[0] = F_ACK;
    put32(out + 1, s->snd_nxt);
    put32(out + 5, s->rcv_nxt);
    put16(out + 9, local_rwnd(s));               /* FEAT-073 flow control */
    size_t extra = write_sack(s, out + STREAM_HEADER);
    if (extra) out[0] |= F_SACK;
    s->send(s->ctx, out, STREAM_HEADER + extra);
}

/* segment pending bytes / queued FIN into the send window and emit them */
static void pump_send(stream_t *s, uint32_t now) {
    /* in-flight bounded by min(cwnd, peer rwnd, cap) — but always allow ≥1 segment
     * so a zero-window receiver still gets probed (the reply re-advertises rwnd). */
    uint32_t win = s->cwnd < STREAM_WIN_MAX ? s->cwnd : STREAM_WIN_MAX;
    if (s->peer_rwnd < win) win = s->peer_rwnd;
    if (win < 1) win = 1;
    while ((s->snd_nxt - s->snd_una) < win) {
        seg_t *seg = &s->sbuf[s->snd_nxt % STREAM_WIN_MAX];
        if (s->pend_count > 0) {
            size_t take = s->pend_count < STREAM_SEG_PAYLOAD ? s->pend_count : STREAM_SEG_PAYLOAD;
            for (size_t i = 0; i < take; i++)
                seg->data[i] = s->pend[(s->pend_head + i) % PEND_CAP];
            s->pend_head = (s->pend_head + take) % PEND_CAP;
            s->pend_count -= take;
            seg->seq = s->snd_nxt;
            seg->len = (uint16_t)take;
            seg->flags = F_DATA;
        } else if (s->fin_queued) {   /* fin_queued ⇒ !fin_segmented (cleared together below) */
            seg->seq = s->snd_nxt;
            seg->len = 0;
            seg->flags = F_FIN;
            s->fin_queued = 0;
            s->fin_segmented = 1;
        } else {
            break;
        }
        seg->present = 1;
        seg->retx = 0;
        seg->sacked = 0;
        seg->sent_at = now;
        if (s->rto_deadline == 0) s->rto_deadline = now + s->rto;
        s->snd_nxt++;
        emit_seg(s, seg);
    }
}

/* retransmit unacked, un-SACK'd segments below `limit` — so a single lost segment
 * costs a single retransmit, not the whole window (was go-back-N). Fast retransmit
 * limits to the hole(s) below the SACK frontier (segments above it aren't proven
 * lost); an RTO passes limit == snd_nxt to resend the whole outstanding window. */
static void retransmit_range(stream_t *s, uint32_t now, uint32_t limit) {
    for (uint32_t q = s->snd_una; q != s->snd_nxt && (int32_t)(q - limit) < 0; q++) {
        seg_t *seg = &s->sbuf[q % STREAM_WIN_MAX];
        if (seg->sacked) continue;
        emit_seg(s, seg);
        seg->retx = 1;            /* ambiguous from now on (Karn) */
        seg->sent_at = now;
    }
}

/* fast retransmit: resend the hole(s) strictly below the highest SACK'd seq.
 * With no SACK info this falls back to resending just snd_una (classic 3-dup-ACK). */
static void fast_retransmit(stream_t *s, uint32_t now) {
    uint32_t limit = s->snd_una + 1;
    for (uint32_t q = s->snd_una; q != s->snd_nxt; q++) {
        if (!s->sbuf[q % STREAM_WIN_MAX].sacked) continue;
        if ((int32_t)(q + 1 - limit) > 0) limit = q + 1;   /* LCOV_EXCL_BR_LINE: ascending scan always advances */
    }
    retransmit_range(s, now, limit);
}

/* mark segments the peer reported via a SACK bitmap (relative to its cumulative
 * ack), so retransmit_lost skips them. Bits outside the live window are ignored. */
static void apply_sack(stream_t *s, uint32_t ack, uint64_t bm) {
    for (int i = 0; i < 63; i++) {
        if (!(bm & ((uint64_t)1 << i))) continue;
        uint32_t q = ack + 1 + (uint32_t)i;
        if ((int32_t)(q - s->snd_una) < 0 || (int32_t)(q - s->snd_nxt) >= 0) continue;
        seg_t *seg = &s->sbuf[q % STREAM_WIN_MAX];
        if (seg->seq == q) seg->sacked = 1;   /* LCOV_EXCL_BR_LINE: in-window q ⇒ slot holds seq==q */
    }
}

/* process a peer's cumulative ack (next seq it expects). Serial-number (RFC 1982)
 * comparisons so the checks stay correct across a 32-bit wrap, and a replayed/old
 * ack (diff <= 0) never moves snd_una (BUG-049). A dup-ack (== snd_una, window
 * non-empty) drives fast retransmit; an advancing ack yields an RTT sample. */
/* AIMD: grow cwnd per newly-acked segment — exponential in slow start, +1 per RTT
 * in congestion avoidance (the cwnd_cnt accumulator). Clamped to the buffer cap. */
static void cc_on_ack(stream_t *s, uint32_t acked) {
    if (acked == 0) return;   /* LCOV_EXCL_BR_LINE: the sole caller passes ack - snd_una ≥ 1 */
    if (s->cwnd < s->ssthresh) {
        s->cwnd += acked;                                  /* slow start */
    } else {
        s->cwnd_cnt += acked;                              /* congestion avoidance */
        while (s->cwnd_cnt >= s->cwnd) { s->cwnd_cnt -= s->cwnd; s->cwnd++; }
    }
    if (s->cwnd > STREAM_WIN_MAX) s->cwnd = STREAM_WIN_MAX;
}

/* multiplicative decrease: halve ssthresh; RTO restarts slow start from the initial
 * window, a fast retransmit enters fast recovery at ssthresh. */
static void cc_on_loss(stream_t *s, int is_rto) {
    uint32_t half = s->cwnd / 2;
    s->ssthresh = half > STREAM_SSTHRESH_MIN ? half : STREAM_SSTHRESH_MIN;
    s->cwnd = is_rto ? STREAM_IW : s->ssthresh;
    s->cwnd_cnt = 0;
}

static void on_ack(stream_t *s, uint32_t ack, uint32_t now) {
    if ((int32_t)(ack - s->snd_una) > 0 && (int32_t)(ack - s->snd_nxt) <= 0) {
        seg_t *t = &s->sbuf[(ack - 1) % STREAM_WIN_MAX];   /* newest newly-acked seg */
        if (t->seq == ack - 1)   /* LCOV_EXCL_BR_LINE: newest in-window ack ⇒ slot holds seq==ack-1 */
            if (!t->retx) rtt_update(s, now - t->sent_at);  /* Karn: no sample for a retransmit */
        cc_on_ack(s, (uint32_t)(ack - s->snd_una));        /* grow the congestion window */
        s->snd_una = ack;
        s->dupacks = 0;
        s->rto_deadline = (s->snd_una == s->snd_nxt) ? 0 : now + s->rto;
    } else if (ack == s->snd_una && s->snd_una != s->snd_nxt) {
        if (s->peer_rwnd == 0) {
            s->dupacks = 0;                            /* closed-window persist, not congestion */
        } else if (++s->dupacks == STREAM_DUPACK_THRESH) {  /* fast retransmit + fast recovery */
            cc_on_loss(s, 0);
            fast_retransmit(s, now);
            s->rto_deadline = now + s->rto;
        }
    }
}

int stream_write(stream_t *s, const unsigned char *data, size_t len, uint32_t now_ms) {
    size_t space = PEND_CAP - s->pend_count;
    size_t n = len < space ? len : space;
    for (size_t i = 0; i < n; i++)
        s->pend[(s->pend_head + s->pend_count + i) % PEND_CAP] = data[i];
    s->pend_count += n;
    pump_send(s, now_ms);
    return (int)n;
}

void stream_input(stream_t *s, const unsigned char *seg, size_t len, uint32_t now_ms) {
    if (len < STREAM_HEADER) return;
    uint8_t flags = seg[0];
    uint32_t seq = get32(seg + 1);
    uint32_t ack = get32(seg + 5);
    s->peer_rwnd = get16(seg + 9);               /* FEAT-073: peer's advertised window */
    const unsigned char *payload = seg + STREAM_HEADER;
    size_t plen = len - STREAM_HEADER;

    if ((flags & F_SACK) && len >= STREAM_HEADER + STREAM_SACK_BYTES)
        apply_sack(s, ack, get64(seg + STREAM_HEADER));   /* before on_ack: skip these on retransmit */

    on_ack(s, ack, now_ms);

    if (!(flags & (F_DATA | F_FIN))) return;  /* pure ack — no reply */

    /* buffer if within the receive window and not already present. Serial-number
     * comparison (wrap-safe, no rcv_nxt+WIN overflow): a replayed/old seg has
     * diff < 0 and is dropped, so a replayed datagram can't perturb state (BUG-049). */
    if ((int32_t)(seq - s->rcv_nxt) >= 0 && (int32_t)(seq - s->rcv_nxt) < STREAM_WIN_MAX) {
        seg_t *r = &s->rbuf[seq % STREAM_WIN_MAX];
        if (!r->present) {
            r->present = 1;
            r->seq = seq;
            r->flags = flags;
            r->len = (uint16_t)(plen < STREAM_SEG_PAYLOAD ? plen : STREAM_SEG_PAYLOAD);
            if (r->len) memcpy(r->data, payload, r->len);
        }
    }

    /* deliver contiguous segments, respecting delivery-buffer backpressure */
    for (;;) {
        seg_t *r = &s->rbuf[s->rcv_nxt % STREAM_WIN_MAX];
        if (!r->present) break;
        /* the slot for rcv_nxt only ever holds seq==rcv_nxt (buffering bounds seq to
         * [rcv_nxt, rcv_nxt+WIN)); the mismatch break is defensive. */
        if (r->seq != s->rcv_nxt) break;   /* LCOV_EXCL_BR_LINE */
        if (r->len > DELIV_CAP - s->deliv_count) break;  /* app must drain first */
        for (size_t i = 0; i < r->len; i++)
            s->deliv[(s->deliv_head + s->deliv_count + i) % DELIV_CAP] = r->data[i];
        s->deliv_count += r->len;
        if (r->flags & F_FIN) s->peer_fin = 1;
        r->present = 0;
        s->rcv_nxt++;
        if (s->peer_fin) break;  /* nothing follows FIN */
    }

    emit_ack(s);
}

int stream_read(stream_t *s, unsigned char *out, size_t cap) {
    size_t n = s->deliv_count < cap ? s->deliv_count : cap;
    for (size_t i = 0; i < n; i++)
        out[i] = s->deliv[(s->deliv_head + i) % DELIV_CAP];
    s->deliv_head = (s->deliv_head + n) % DELIV_CAP;
    s->deliv_count -= n;
    return (int)n;
}

void stream_tick(stream_t *s, uint32_t now_ms) {
    pump_send(s, now_ms);
    int due = s->rto_deadline && now_ms >= s->rto_deadline;
    /* a fully-acked stream clears rto_deadline, so a due deadline implies outstanding
     * data; the snd_una!=snd_nxt term is defensive (its false arm is unreachable). */
    if (due && s->snd_una != s->snd_nxt) {   /* LCOV_EXCL_BR_LINE */
        if (s->peer_rwnd != 0) {
            s->rto = s->rto < STREAM_RTO_MAX / 2 ? s->rto * 2 : STREAM_RTO_MAX;  /* Karn backoff */
            cc_on_loss(s, 1);                      /* real timeout: collapse cwnd, restart slow start */
        }
        /* a closed window is persist-mode probing, not congestion: keep the RTO steady
         * (don't back off the probe interval) and leave cwnd untouched. */
        retransmit_range(s, now_ms, s->snd_nxt);   /* resend (probe the window / recover loss) */
        s->rto_deadline = now_ms + s->rto;
    }
}

void stream_finish(stream_t *s, uint32_t now_ms) {
    s->fin_queued = 1;
    pump_send(s, now_ms);
}

size_t stream_readable(const stream_t *s) { return s->deliv_count; }

int stream_peer_finished(const stream_t *s) {
    return s->peer_fin && s->deliv_count == 0;
}

int stream_send_done(const stream_t *s) {
    return s->fin_segmented && s->snd_una == s->snd_nxt;
}

uint32_t stream_rto_ms(const stream_t *s) { return s->rto; }
uint32_t stream_cwnd(const stream_t *s) { return s->cwnd; }
uint32_t stream_inflight(const stream_t *s) { return s->snd_nxt - s->snd_una; }
