/**
 * @file peers.c
 * @brief Multi-node cluster transport helpers for nornd. See peers.h.
 */

#include "peers.h"

#include <string.h>

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int nornd_peer_parse(const char *spec, nornd_peer_t *out) {
    if (!spec || !out) return -1;
    memset(out, 0, sizeof(*out));

    /* The pubkey is the 64 hex chars before an optional '@endpoint'. */
    const char *at = strchr(spec, '@');
    size_t hexlen = at ? (size_t)(at - spec) : strlen(spec);
    if (hexlen != 2 * NORND_PEER_PUBKEY) return -1;
    for (int i = 0; i < NORND_PEER_PUBKEY; i++) {
        int hi = hexval(spec[2 * i]);
        int lo = hexval(spec[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out->pubkey[i] = (unsigned char)((hi << 4) | lo);
    }
    if (!at) return 0; /* pubkey only -> DHT resolution */

    /* Direct endpoint: host is up to the last ':' (IPv6-safe), then the port. */
    const char *hp = at + 1;
    const char *colon = strrchr(hp, ':');
    if (!colon || colon == hp) return -1;
    size_t hlen = (size_t)(colon - hp);
    if (hlen >= sizeof(out->host)) return -1;
    memcpy(out->host, hp, hlen);
    out->host[hlen] = '\0';

    const char *ps = colon + 1;
    if (*ps == '\0') return -1;
    unsigned long p = 0;
    for (const char *q = ps; *q; q++) {
        if (*q < '0' || *q > '9') return -1;
        p = p * 10 + (unsigned long)(*q - '0');
        if (p > 65535) return -1;
    }
    if (p == 0) return -1;
    out->port = (uint16_t)p;
    out->direct = 1;
    return 0;
}

int nornd_frame_encode(const unsigned char *payload, size_t len,
                       unsigned char *out, size_t cap) {
    if (!payload || !out || len > NORND_FRAME_MAX || (size_t)4 + len > cap)
        return -1;
    out[0] = (unsigned char)(len >> 24);
    out[1] = (unsigned char)(len >> 16);
    out[2] = (unsigned char)(len >> 8);
    out[3] = (unsigned char)len;
    memcpy(out + 4, payload, len);
    return (int)(4 + len);
}

void nornd_framer_reset(nornd_framer_t *f) {
    f->start = 0;
    f->end = 0;
}

int nornd_framer_push(nornd_framer_t *f, const unsigned char *data, size_t len) {
    /* Won't fit even after reclaiming already-read bytes → desynced stream. */
    if (len > sizeof(f->buf) - (f->end - f->start)) {
        nornd_framer_reset(f);
        return -1;
    }
    /* Compact unread bytes to the front if needed to make room at the end. */
    if (len > sizeof(f->buf) - f->end) {
        memmove(f->buf, f->buf + f->start, f->end - f->start);
        f->end -= f->start;
        f->start = 0;
    }
    memcpy(f->buf + f->end, data, len);
    f->end += len;
    return 0;
}

int nornd_framer_next(nornd_framer_t *f, const unsigned char **payload,
                      size_t *plen) {
    size_t avail = f->end - f->start;
    if (avail < 4) return 0;
    const unsigned char *p = f->buf + f->start;
    size_t body = ((size_t)p[0] << 24) | ((size_t)p[1] << 16) |
                  ((size_t)p[2] << 8) | (size_t)p[3];
    if (body > NORND_FRAME_MAX) return -1; /* protocol violation */
    if (avail < 4 + body) return 0;        /* incomplete */
    *payload = p + 4;
    *plen = body;
    f->start += 4 + body;
    return 1;
}
