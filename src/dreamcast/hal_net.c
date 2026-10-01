/* KallistiOS network boundary: Broadband Adapter, DHCP, ICMP echo and DNS.
 * GPL-2.0-or-later. Compiled with KOS structure packing. Nothing here blocks
 * the caller: the OS layer polls so the display and Ctrl+C stay responsive. */
#include <kos.h>
#include <kos/net.h>
#include <dc/fs_dcload.h>
#include <dc/net/broadband_adapter.h>
#include <dc/net/lan_adapter.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include "dreamcast/net.h"

static volatile int net_started, net_finished;

static void *net_thread(void *arg)
{
    (void)arg;
    /* INIT_NET would block boot on DHCP and also probe the serial port for a
     * W5500 adapter, which the planned serial SD card will need. Register only
     * the two Ethernet adapters that sit on the G2 bus. */
    bba_init();
    la_init();
    /* DHCP can wait a minute without a server: never do this on the UI path. */
    net_init(0);
    net_finished = 1;
    netif_t *n = net_default_dev;
    if (n)
        printf("EmuTOS net: %s %d.%d.%d.%d gw %d.%d.%d.%d dns %d.%d.%d.%d\n", n->name, n->ip_addr[0], n->ip_addr[1],
               n->ip_addr[2], n->ip_addr[3], n->gateway[0], n->gateway[1], n->gateway[2], n->gateway[3], n->dns[0],
               n->dns[1], n->dns[2], n->dns[3]);
    else
        printf("EmuTOS net: no network adapter\n");
    return NULL;
}
void dc_hal_net_start(void)
{
    if (net_started)
        return;
    net_started = 1;
    /* dcload-ip owns the Ethernet adapter and services the console and /pc.
     * Reinitializing that adapter here would strand its native syscalls. Keep
     * it with the loader for network boots; disc boots use the KOS stack. */
    if (dcload_type == DCLOAD_TYPE_IP) {
        net_finished = 1;
        printf("EmuTOS net: Ethernet reserved for dcload-ip console and files\n");
        return;
    }
    kthread_attr_t attr = {.stack_size = 16384, .prio = PRIO_DEFAULT - 1, .label = "EmuTOS net", .create_detached = 1};
    if (!thd_create_ex(&attr, net_thread, NULL)) {
        net_finished = 1;
        printf("EmuTOS: cannot start network thread\n");
    }
}
void dc_hal_net_info(struct dc_net_info *info)
{
    netif_t *n = net_default_dev;
    if (!net_finished) {
        info->state = DC_NET_STARTING;
        n = NULL;
    } else if (!n)
        info->state = DC_NET_NO_ADAPTER;
    if (n) {
        info->flags = n->flags;
        info->mtu = n->mtu;
        memcpy(info->mac, n->mac_addr, 6);
        memcpy(info->ip, n->ip_addr, 4);
        memcpy(info->netmask, n->netmask, 4);
        memcpy(info->gateway, n->gateway, 4);
        memcpy(info->dns, n->dns, 4);
        memcpy(info->broadcast, n->broadcast, 4);
        strlcpy(info->name, n->name ? n->name : "", sizeof(info->name));
        strlcpy(info->description, n->descr ? n->descr : "", sizeof(info->description));
        info->state = (n->ip_addr[0] | n->ip_addr[1] | n->ip_addr[2] | n->ip_addr[3]) ? DC_NET_UP : DC_NET_NO_ADDRESS;
    }
    if (net_finished) {
        net_ipv4_stats_t s = net_ipv4_get_stats();
        info->ip_sent = s.pkt_sent;
        info->ip_send_failed = s.pkt_send_failed;
        info->ip_received = s.pkt_recv;
        info->ip_bad = s.pkt_recv_bad_size + s.pkt_recv_bad_chksum + s.pkt_recv_bad_proto;
    }
}

/* ICMP: KOS has one global reply hook, so keep one request in flight. */
static volatile int ping_waiting, ping_done;
static uint8_t ping_target[4];
static uint16_t ping_seq;
static uint64_t ping_start;
static struct dc_ping_result ping_reply;
static void ping_cb(const uint8_t *ip, uint16_t seq, uint64_t delta_us, uint8_t ttl, const uint8_t *data, size_t len)
{
    if (!ping_waiting || seq != ping_seq || memcmp(ip, ping_target, 4))
        return;
    uint64_t rtt = delta_us != (uint64_t)-1 ? delta_us : timer_us_gettime64() - ping_start;
    memcpy(ping_reply.from, ip, 4);
    ping_reply.rtt_us = rtt > 0xffffffffu ? 0xffffffffu : (uint32_t)rtt;
    ping_reply.ttl = ttl;
    ping_reply.reply_bytes = len > 16 ? (uint32_t)len - 16 : 0; /* strip ICMP header and timestamp */
    ping_done = 1;
}
int dc_hal_ping_send(const uint8_t ip[4], uint32_t seq, uint32_t size)
{
    netif_t *n = net_default_dev;
    if (!net_finished || !n || !(n->ip_addr[0] | n->ip_addr[1] | n->ip_addr[2] | n->ip_addr[3]))
        return DC_NET_ERR_DOWN;
    static uint8_t payload[1400];
    if (size > sizeof(payload))
        return DC_NET_ERR_ARGS;
    for (uint32_t i = 0; i < size; i++)
        payload[i] = 'a' + i % 23;
    memcpy(ping_target, ip, 4);
    ping_seq = seq;
    ping_done = 0;
    net_icmp_echo_cb = ping_cb;
    ping_start = timer_us_gettime64();
    ping_waiting = 1;
    if (net_icmp_send_echo(n, ip, 0xd0c5, (uint16_t)seq, payload, size) < 0) {
        ping_waiting = 0;
        return DC_NET_ERR_SEND;
    }
    return 0;
}
int dc_hal_ping_poll(struct dc_ping_result *result)
{
    if (!ping_done)
        return 0;
    ping_waiting = 0;
    *result = ping_reply;
    return 1;
}
void dc_hal_ping_cancel(void) { ping_waiting = 0; }

/* DNS: getaddrinfo blocks for its whole timeout, so run it on a helper thread.
 * An abandoned lookup keeps the slot busy until it finishes on its own. */
static volatile int dns_state; /* 0 idle, 1 running, 2 done, 3 abandoned */
static volatile int dns_result;
static char dns_name[256];
static uint8_t dns_ip[4];
static int dns_lookup(const char *name, const uint8_t server[4], uint8_t out[4])
{
    static uint16_t counter;
    uint8_t packet[DC_DNS_MAX_PACKET];
    uint16_t id = (uint16_t)(timer_us_gettime64() ^ (++counter * 40503u));
    unsigned size = dc_dns_build_query(packet, sizeof(packet), id, name);
    if (!size)
        return DC_NET_ERR_ARGS;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return DC_NET_ERR_SEND;
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(53)};
    memcpy(&to.sin_addr.s_addr, server, 4);
    int result = DC_NET_ERR_TIMEOUT;
    if (connect(sock, (struct sockaddr *)&to, sizeof(to))) {
        close(sock);
        return DC_NET_ERR_SEND;
    }
    for (int attempt = 0; attempt < 3 && result == DC_NET_ERR_TIMEOUT; attempt++) {
        if (send(sock, packet, size, 0) < 0) {
            result = DC_NET_ERR_SEND;
            break;
        }
        uint64_t deadline = timer_ms_gettime64() + 2500;
        for (;;) {
            uint64_t now = timer_ms_gettime64();
            struct pollfd pfd = {.fd = sock, .events = POLLIN};
            if (now >= deadline || poll(&pfd, 1, (int)(deadline - now)) != 1)
                break;
            uint8_t reply[DC_DNS_MAX_PACKET];
            ssize_t n = recv(sock, reply, sizeof(reply), 0);
            if (n < 0)
                break;
            int rc = dc_dns_parse_response(reply, (unsigned)n, id, name, out);
            if (rc != 1) { /* not ours: keep waiting */
                result = rc;
                break;
            }
        }
    }
    close(sock);
    return result;
}
static void *dns_thread(void *arg)
{
    (void)arg;
    uint8_t server[4];
    netif_t *n = net_default_dev;
    int rc = DC_NET_ERR_DOWN;
    if (n) {
        memcpy(server, n->dns, 4);
        rc = (server[0] | server[1] | server[2] | server[3]) ? dns_lookup(dns_name, server, dns_ip) : DC_NET_ERR_SERVER;
    }
    dns_result = rc == 0 ? 1 : rc;
    dns_state = dns_state == 3 ? 0 : 2;
    return NULL;
}
int dc_hal_resolve_start(const char *host)
{
    netif_t *n = net_default_dev;
    if (!net_finished || !n || !(n->ip_addr[0] | n->ip_addr[1] | n->ip_addr[2] | n->ip_addr[3]))
        return DC_NET_ERR_DOWN;
    if (dns_state)
        return DC_NET_ERR_BUSY;
    if (strlen(host) >= sizeof(dns_name))
        return DC_NET_ERR_ARGS;
    strcpy(dns_name, host);
    dns_state = 1;
    kthread_attr_t attr = {.stack_size = 16384, .prio = PRIO_DEFAULT, .label = "EmuTOS DNS", .create_detached = 1};
    if (!thd_create_ex(&attr, dns_thread, NULL)) {
        dns_state = 0;
        return DC_NET_ERR_BUSY;
    }
    return 0;
}
int dc_hal_resolve_poll(uint8_t ip[4])
{
    if (dns_state != 2)
        return 0;
    int r = dns_result;
    if (r > 0)
        memcpy(ip, dns_ip, 4);
    dns_state = 0;
    return r;
}
void dc_hal_resolve_cancel(void)
{
    /* Let the helper finish and free the slot itself. */
    if (dns_state == 1)
        dns_state = 3;
    else if (dns_state == 2)
        dns_state = 0;
}
