/* Native ABI network services. GPL-2.0-or-later.
 * Waits are polled here (outside the KOS packing boundary) so the display keeps
 * updating and Ctrl+C can interrupt them. */
#include "emutos.h"
#include "string.h"
#include "dreamcast/hal.h"
#include "dreamcast/net.h"

extern void dc_poll(void);
extern int dc_console_break(void);

long dc_net_info(void *buffer, uint32_t bytes)
{
    struct dc_net_info *info = buffer;
    if (!buffer)
        return bytes ? -64 : (long)sizeof(*info);
    if (bytes < sizeof(*info))
        return -64;
    memset(info, 0, sizeof(*info));
    info->version = DC_NET_VERSION;
    info->bytes = sizeof(*info);
    dc_hal_net_info(info);
    return sizeof(*info);
}
/* Returns 1 when finished, 0 on timeout, -1 on Ctrl+C. */
static int wait_until(int (*done)(void *), void *arg, uint32_t timeout_ms)
{
    unsigned long start = dc_millis();
    while (!done(arg)) {
        if (dc_console_break())
            return -1;
        if (dc_millis() - start >= timeout_ms)
            return 0;
        dc_poll();
        dc_sleep(2);
    }
    return 1;
}
static int ping_done(void *result) { return dc_hal_ping_poll(result); }
long dc_net_ping(const uint8_t ip[4], uint32_t seq, uint32_t size, uint32_t timeout_ms, void *result, uint32_t bytes)
{
    struct dc_ping_result *out = result;
    if (!ip || !out || bytes < sizeof(*out) || size > 1400 || timeout_ms < 1 || timeout_ms > 30000)
        return DC_NET_ERR_ARGS;
    memset(out, 0, sizeof(*out));
    out->version = DC_NET_VERSION;
    out->bytes = sizeof(*out);
    int rc = dc_hal_ping_send(ip, seq, size);
    if (rc < 0)
        return rc;
    rc = wait_until(ping_done, out, timeout_ms);
    if (rc == 1)
        return DC_NET_OK;
    dc_hal_ping_cancel();
    return rc < 0 ? DC_NET_ERR_BREAK : DC_NET_ERR_TIMEOUT;
}
struct lookup { uint8_t *ip; int result; };
static int lookup_done(void *arg)
{
    struct lookup *l = arg;
    return (l->result = dc_hal_resolve_poll(l->ip)) != 0;
}
long dc_net_resolve(const char *host, uint32_t timeout_ms, uint8_t ip[4])
{
    if (!host || !*host || !ip || timeout_ms < 1 || timeout_ms > 60000)
        return DC_NET_ERR_ARGS;
    int rc = dc_hal_resolve_start(host);
    if (rc < 0)
        return rc;
    struct lookup l = {ip, 0};
    rc = wait_until(lookup_done, &l, timeout_ms);
    if (rc == 1)
        return l.result > 0 ? DC_NET_OK : l.result;
    dc_hal_resolve_cancel();
    return rc < 0 ? DC_NET_ERR_BREAK : DC_NET_ERR_TIMEOUT;
}
