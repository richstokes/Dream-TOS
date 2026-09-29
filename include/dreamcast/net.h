/* Native networking snapshot and results. GPL-2.0-or-later.
 * Fixed-width scalars only: identical layout at GEM and KOS packing boundaries.
 * IPv4 addresses are four bytes in network order (a.b.c.d = bytes 0..3).
 * Backed by the KallistiOS network stack and the Broadband Adapter driver. */
#ifndef EMUTOS_DC_NET_H
#define EMUTOS_DC_NET_H
#include <stdint.h>

#define DC_NET_VERSION 1

/* dc_net_info.state */
#define DC_NET_STARTING 0   /* stack is initializing / waiting for DHCP */
#define DC_NET_NO_ADAPTER 1 /* no network adapter was detected */
#define DC_NET_NO_ADDRESS 2 /* adapter present but no IPv4 address yet */
#define DC_NET_UP 3         /* adapter configured with an IPv4 address */

struct dc_net_info {
    uint32_t version, bytes;
    uint32_t state;            /* DC_NET_* */
    uint32_t flags;            /* KOS netif flags; 0 without an adapter */
    uint32_t mtu;
    uint8_t mac[6];
    uint8_t reserved[2];
    uint8_t ip[4], netmask[4], gateway[4], dns[4], broadcast[4];
    char name[16];             /* Interface name, e.g. "bba0" */
    char description[48];      /* Adapter description from the driver */
    uint32_t ip_sent, ip_send_failed, ip_received, ip_bad;
};

/* Ping/resolve results (also the OS-side return values). Negative = error. */
#define DC_NET_OK 0
#define DC_NET_ERR_ARGS -64
#define DC_NET_ERR_DOWN -1     /* no configured adapter */
#define DC_NET_ERR_TIMEOUT -2  /* no reply before the deadline */
#define DC_NET_ERR_SEND -3     /* packet could not be sent (e.g. no ARP reply) */
#define DC_NET_ERR_NOTFOUND -4 /* name does not exist / has no IPv4 address */
#define DC_NET_ERR_BUSY -5     /* previous request is still running */
#define DC_NET_ERR_BREAK -6    /* interrupted with Ctrl+C */
#define DC_NET_ERR_SERVER -7   /* DNS server failure or refusal */

struct dc_ping_result {
    uint32_t version, bytes;
    uint8_t from[4];           /* Address of the responder */
    uint32_t rtt_us;           /* Round-trip time in microseconds */
    uint32_t ttl;
    uint32_t reply_bytes;      /* ICMP payload bytes echoed back */
};

/* OS-side entry points. Applications use dc_native_api.net_* instead.
 * dc_net_info: NULL/0 returns the byte count; success fills the snapshot.
 * dc_net_ping: send one echo of `size` payload bytes (0..1400) and wait up to
 *   timeout_ms (1..30000). Servicing input while waiting; Ctrl+C aborts.
 * dc_net_resolve: IPv4 lookup through the DHCP-provided DNS server; waits up to
 *   timeout_ms. Fills ip[4]. The hostname must be a valid DNS name. */
long dc_net_info(void *buffer, uint32_t bytes);
long dc_net_ping(const uint8_t ip[4], uint32_t seq, uint32_t size, uint32_t timeout_ms, void *result, uint32_t bytes);
long dc_net_resolve(const char *host, uint32_t timeout_ms, uint8_t ip[4]);

/* DNS wire format (pure C, host-tested; src/dreamcast/dns.c). Only IPv4 "A"
 * queries. build returns the packet length or 0 for an invalid name; parse
 * returns 0 with ip filled, DC_NET_ERR_NOTFOUND / DC_NET_ERR_SERVER for a
 * definitive answer, or 1 when the packet is not the reply to this query
 * (wrong ID, question or malformed) and must be ignored. */
#define DC_DNS_MAX_PACKET 512
unsigned dc_dns_build_query(uint8_t *buf, unsigned capacity, uint16_t id, const char *name);
int dc_dns_parse_response(const uint8_t *packet, unsigned length, uint16_t id, const char *name, uint8_t ip[4]);

/* Non-blocking KOS primitives (src/dreamcast/hal_net.c). */
void dc_hal_net_start(void);
void dc_hal_net_info(struct dc_net_info *info);
int dc_hal_ping_send(const uint8_t ip[4], uint32_t seq, uint32_t size);
int dc_hal_ping_poll(struct dc_ping_result *result);   /* 1 reply, 0 pending */
void dc_hal_ping_cancel(void);
int dc_hal_resolve_start(const char *host);            /* 0 started, <0 DC_NET_ERR_* */
int dc_hal_resolve_poll(uint8_t ip[4]);                /* 0 pending, 1 done, <0 error */
void dc_hal_resolve_cancel(void);
#endif
