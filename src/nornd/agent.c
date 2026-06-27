/**
 * @file agent.c
 * @brief ssh-agent signing client (FEAT-028). See agent.h.
 *
 * SSH agent protocol (OpenSSH PROTOCOL.agent): every message is a uint32
 * big-endian length followed by that many bytes; "string" fields are themselves
 * uint32-length-prefixed. We issue SSH_AGENTC_SIGN_REQUEST for an ed25519 key
 * and parse SSH_AGENT_SIGN_RESPONSE back into the raw 64-byte signature.
 */

#include "agent.h"

#include <string.h>

#define SSH_AGENTC_SIGN_REQUEST 13
#define SSH_AGENT_SIGN_RESPONSE 14

static const char ED[] = "ssh-ed25519";
#define ED_LEN 11 /* strlen("ssh-ed25519") */

static void put_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}
static uint32_t get_u32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int nornd_agent_encode_sign_request(const unsigned char pub[32],
                                    const unsigned char *data, size_t dlen,
                                    unsigned char *out, size_t cap,
                                    size_t *outlen) {
    if (!pub || !out || !outlen) return -1;
    if (dlen && !data) return -1;
    /* key blob = string "ssh-ed25519" + string pub[32] */
    size_t keyblob = 4 + ED_LEN + 4 + 32;
    /* body = type byte + string keyblob + string data + uint32 flags */
    size_t body = 1 + (4 + keyblob) + (4 + dlen) + 4;
    size_t total = 4 + body;
    if (total > cap || total > NORND_AGENT_REQ_MAX) return -1;

    unsigned char *p = out;
    put_u32(p, (uint32_t)body); p += 4;            /* outer frame length */
    *p++ = SSH_AGENTC_SIGN_REQUEST;
    put_u32(p, (uint32_t)keyblob); p += 4;         /* key blob string */
    put_u32(p, ED_LEN); p += 4;
    memcpy(p, ED, ED_LEN); p += ED_LEN;
    put_u32(p, 32); p += 4;
    memcpy(p, pub, 32); p += 32;
    put_u32(p, (uint32_t)dlen); p += 4;            /* data string */
    if (dlen) { memcpy(p, data, dlen); p += dlen; }
    put_u32(p, 0);                                 /* flags */
    *outlen = total;
    return (int)total;
}

int nornd_agent_decode_sign_response(const unsigned char *body, size_t blen,
                                     unsigned char sig[NORND_AGENT_SIGBYTES]) {
    if (!body || !sig) return -1;
    if (blen < 1 || body[0] != SSH_AGENT_SIGN_RESPONSE) return -1;
    size_t pos = 1;
    if (pos + 4 > blen) return -1;
    uint32_t sblen = get_u32(body + pos);
    pos += 4;
    if ((size_t)sblen > blen - pos) return -1;

    /* signature blob = string "ssh-ed25519" + string sig[64] */
    const unsigned char *s = body + pos;
    size_t left = sblen;
    if (left < 4) return -1;
    uint32_t tlen = get_u32(s); s += 4; left -= 4;
    if ((size_t)tlen > left) return -1;
    if (tlen != ED_LEN || memcmp(s, ED, ED_LEN) != 0) return -1;
    s += tlen; left -= tlen;
    if (left < 4) return -1;
    uint32_t glen = get_u32(s); s += 4; left -= 4;
    if (glen != NORND_AGENT_SIGBYTES || (size_t)glen > left) return -1;
    memcpy(sig, s, NORND_AGENT_SIGBYTES);
    return 0;
}

int nornd_agent_sign_io(const nornd_agent_io_t *io, const unsigned char pub[32],
                        const unsigned char *data, size_t dlen,
                        unsigned char sig[NORND_AGENT_SIGBYTES]) {
    if (!io || !io->write_all || !io->read_all) return -1;
    unsigned char req[NORND_AGENT_REQ_MAX];
    size_t reqlen = 0;
    if (nornd_agent_encode_sign_request(pub, data, dlen, req, sizeof(req),
                                        &reqlen) < 0)
        return -1;
    if (io->write_all(io->ctx, req, reqlen) != 0) return -1;

    unsigned char lenb[4];
    if (io->read_all(io->ctx, lenb, 4) != 0) return -1;
    uint32_t blen = get_u32(lenb);
    if (blen == 0 || blen > NORND_AGENT_RESP_MAX) return -1;
    unsigned char body[NORND_AGENT_RESP_MAX];
    if (io->read_all(io->ctx, body, blen) != 0) return -1;
    return nornd_agent_decode_sign_response(body, blen, sig);
}
