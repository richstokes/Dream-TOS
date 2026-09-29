/* Run the real ping/nslookup/ifconfig sources under host sanitizers against a
 * scripted OS. NET_STATE selects the adapter state; hosts are fixed fixtures. */
#include "app.h"
#include "dreamcast/net.h"
#include <stdlib.h>
#include <string.h>
static unsigned long clock_ms;
static int keys;
static long gemdos(int op, ...)
{
    if (op == 0x0b) return keys > 0 ? -1 : 0;   /* Cconis */
    if (op == 0x08) { keys--; return 3; }       /* Cnecin: Ctrl+C */
    return 0;
}
static unsigned long millis(void) { return clock_ms; }
static void yield(void) { clock_ms += 250; }
static long info(void *buffer, uint32_t bytes)
{
    struct dc_net_info *n = buffer;
    if (bytes != sizeof(*n)) return -64;
    memset(n, 0, sizeof(*n));
    const char *state = getenv("NET_STATE");
    n->version = DC_NET_VERSION; n->bytes = sizeof(*n);
    n->state = state ? (uint32_t)atoi(state) : DC_NET_UP;
    if (n->state >= DC_NET_NO_ADDRESS) {
        strcpy(n->name, "bba0"); strcpy(n->description, "Broadband Adapter (HIT-0400)"); n->mtu = 1500;
        memcpy(n->mac, "\x00\xd0\xf1\x0a\x0b\x0c", 6);
    }
    if (n->state == DC_NET_UP) {
        memcpy(n->ip, (uint8_t[]){192,168,1,50}, 4); memcpy(n->netmask, (uint8_t[]){255,255,255,0}, 4);
        memcpy(n->gateway, (uint8_t[]){192,168,1,1}, 4); memcpy(n->dns, (uint8_t[]){192,168,1,1}, 4);
        memcpy(n->broadcast, (uint8_t[]){192,168,1,255}, 4);
    }
    n->ip_sent = 7; n->ip_received = 6; n->ip_send_failed = 1; n->ip_bad = 0;
    return sizeof(*n);
}
static long ping(const uint8_t ip[4], uint32_t seq, uint32_t size, uint32_t timeout, void *result, uint32_t bytes)
{
    struct dc_ping_result *r = result;
    if (bytes != sizeof(*r)) return -64;
    if (ip[3] == 9) return DC_NET_ERR_TIMEOUT;
    if (ip[3] == 8) return DC_NET_ERR_SEND;
    memset(r, 0, sizeof(*r));
    memcpy(r->from, ip, 4); r->rtt_us = 1000 * seq + 500; r->ttl = 64; r->reply_bytes = size;
    return 0;
}
static long resolve(const char *host, uint32_t timeout, uint8_t ip[4])
{
    if (!strcmp(host, "example.test")) { memcpy(ip, (uint8_t[]){93,184,216,34}, 4); return 0; }
    if (!strcmp(host, "slow.test")) return DC_NET_ERR_TIMEOUT;
    return DC_NET_ERR_NOTFOUND;
}
static struct dc_native_api api = {.version = 1, .size = sizeof(api), .gemdos = gemdos, .yield = yield, .millis = millis,
    .net_info = info, .net_ping = ping, .net_resolve = resolve};
const struct dc_native_api *dc_os = &api;
extern int app_main(int, char **);
int main(int argc, char **argv)
{
    if (getenv("NET_OLD_API")) api.size = offsetof(struct dc_native_api, net_info);
    if (getenv("NET_BREAK")) keys = 1;
    return app_main(argc, argv);
}
