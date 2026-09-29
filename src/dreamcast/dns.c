/* Minimal DNS A-record wire format. GPL-2.0-or-later. Pure C, no KOS calls.
 * Unlike a general resolver it accepts a reply only if the transaction ID and
 * the echoed question both match: late duplicate answers to earlier queries
 * (same ephemeral UDP port) must never be mistaken for the current lookup. */
#include <stdint.h>
#include <stddef.h>

static unsigned name_length(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}
#include "dreamcast/net.h"

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

unsigned dc_dns_build_query(uint8_t *buf, unsigned capacity, uint16_t id, const char *name)
{
    unsigned len = name_length(name), o = 12, start = 0;
    if (len && name[len - 1] == '.')
        len--;
    if (!len || len > 253 || capacity < len + 18)
        return 0;
    for (unsigned i = 0; i < 12; i++)
        buf[i] = 0;
    buf[0] = id >> 8;
    buf[1] = id;
    buf[2] = 0x01; /* recursion desired */
    buf[5] = 1;    /* one question */
    for (unsigned i = 0; i <= len; i++) {
        if (i < len && name[i] != '.')
            continue;
        unsigned n = i - start;
        if (!n || n > 63)
            return 0;
        buf[o++] = n;
        for (unsigned k = 0; k < n; k++)
            buf[o++] = name[start + k];
        start = i + 1;
    }
    buf[o++] = 0;
    buf[o++] = 0; buf[o++] = 1; /* QTYPE A */
    buf[o++] = 0; buf[o++] = 1; /* QCLASS IN */
    return o;
}

/* Skip an encoded name at *o; returns 0 on success. Pointers end the name. */
static int skip_name(const uint8_t *p, unsigned n, unsigned *o)
{
    unsigned pos = *o;
    for (;;) {
        if (pos >= n)
            return -1;
        unsigned c = p[pos];
        if (!c) { *o = pos + 1; return 0; }
        if ((c & 0xc0) == 0xc0) {
            if (pos + 2 > n) return -1;
            *o = pos + 2;
            return 0;
        }
        if (c & 0xc0)
            return -1;
        pos += c + 1;
    }
}

int dc_dns_parse_response(const uint8_t *p, unsigned n, uint16_t id, const char *name, uint8_t ip[4])
{
    if (n < 12 || ((p[0] << 8) | p[1]) != id || !(p[2] & 0x80) || ((p[4] << 8) | p[5]) != 1)
        return 1;
    /* The question must echo exactly what we asked (case-insensitive). */
    unsigned o = 12, len = name_length(name);
    if (len && name[len - 1] == '.')
        len--;
    for (unsigned i = 0; i <= len;) {
        unsigned end = i;
        while (end < len && name[end] != '.')
            end++;
        if (o + 1 + (end - i) > n || p[o] != end - i)
            return 1;
        o++;
        for (unsigned k = i; k < end; k++, o++)
            if (lower(p[o]) != lower((unsigned char)name[k]))
                return 1;
        i = end + 1;
    }
    if (o + 5 > n || p[o] != 0 || p[o + 1] != 0 || p[o + 2] != 1 || p[o + 3] != 0 || p[o + 4] != 1)
        return 1;
    o += 5;
    switch (p[3] & 15) {
    case 0: break;
    case 3: return DC_NET_ERR_NOTFOUND;
    default: return DC_NET_ERR_SERVER;
    }
    for (unsigned an = (p[6] << 8) | p[7]; an; an--) {
        if (skip_name(p, n, &o) || o + 10 > n)
            return 1;
        unsigned type = (p[o] << 8) | p[o + 1], cls = (p[o + 2] << 8) | p[o + 3], rdlen = (p[o + 8] << 8) | p[o + 9];
        o += 10;
        if (o + rdlen > n)
            return 1;
        if (type == 1 && cls == 1 && rdlen == 4) {
            for (unsigned k = 0; k < 4; k++)
                ip[k] = p[o + k];
            return 0;
        }
        o += rdlen; /* CNAME and others: keep looking for an A record */
    }
    return DC_NET_ERR_NOTFOUND;
}
