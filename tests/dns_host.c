#include "dreamcast/net.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned reply(uint8_t *b, const uint8_t *q, unsigned qn, int rcode, int cname, int a)
{
    memcpy(b, q, qn); b[2] = 0x81; b[3] = 0x80 | rcode;
    unsigned o = qn, an = 0;
    if (cname) { const uint8_t r[] = {0xc0,12,0,5,0,1,0,0,0,60,0,2,0xc0,12}; memcpy(b+o,r,sizeof r); o += sizeof r; an++; }
    if (a) { const uint8_t r[] = {0xc0,12,0,1,0,1,0,0,0,60,0,4,93,184,216,34}; memcpy(b+o,r,sizeof r); o += sizeof r; an++; }
    b[7] = an; return o;
}
int main(void)
{
    uint8_t q[512], r[512], ip[4] = {0};
    unsigned qn = dc_dns_build_query(q, sizeof q, 0xbeef, "Example.COM.");
    assert(qn == 12 + 1+7 + 1+3 + 1 + 4);
    assert(!memcmp(q + 12, "\7Example\3COM\0\0\1\0\1", 17) && q[0] == 0xbe && q[2] == 1 && q[5] == 1);
    assert(!dc_dns_build_query(q, sizeof q, 1, "") && !dc_dns_build_query(q, sizeof q, 1, "a..b"));
    char big[300]; memset(big, 'a', 299); big[299] = 0;
    assert(!dc_dns_build_query(q, sizeof q, 1, big));
    qn = dc_dns_build_query(q, sizeof q, 0xbeef, "example.com");
    assert(!dc_dns_build_query(q, 10, 1, "example.com"));
    unsigned n = reply(r, q, qn, 0, 0, 1);
    assert(dc_dns_parse_response(r, n, 0xbeef, "EXAMPLE.com", ip) == 0 && !memcmp(ip, "\135\270\330\042", 4));
    n = reply(r, q, qn, 0, 1, 1); memset(ip, 0, 4);
    assert(dc_dns_parse_response(r, n, 0xbeef, "example.com", ip) == 0 && ip[0] == 93);      /* CNAME first */
    /* Stale/duplicate replies for another lookup must be ignored, not accepted. */
    n = reply(r, q, qn, 0, 0, 1);
    assert(dc_dns_parse_response(r, n, 0xbeee, "example.com", ip) == 1);                     /* wrong ID */
    assert(dc_dns_parse_response(r, n, 0xbeef, "google.com", ip) == 1);                      /* wrong question */
    assert(dc_dns_parse_response(r, n, 0xbeef, "example.co", ip) == 1);
    assert(dc_dns_parse_response(r, 5, 0xbeef, "example.com", ip) == 1);                     /* truncated */
    assert(dc_dns_parse_response(r, qn + 6, 0xbeef, "example.com", ip) != 0);                /* cut mid-answer */
    n = reply(r, q, qn, 3, 0, 0);
    assert(dc_dns_parse_response(r, n, 0xbeef, "example.com", ip) == DC_NET_ERR_NOTFOUND);
    n = reply(r, q, qn, 2, 0, 0);
    assert(dc_dns_parse_response(r, n, 0xbeef, "example.com", ip) == DC_NET_ERR_SERVER);
    n = reply(r, q, qn, 0, 0, 0);                                                            /* NOERROR, no answer */
    assert(dc_dns_parse_response(r, n, 0xbeef, "example.com", ip) == DC_NET_ERR_NOTFOUND);
    memcpy(r, q, qn); r[2] = 0;                                                              /* query, not response */
    assert(dc_dns_parse_response(r, qn, 0xbeef, "example.com", ip) == 1);
    puts("dns ok");
    return 0;
}
