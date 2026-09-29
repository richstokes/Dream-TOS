/* Network utilities for EmuCON: ping, nslookup, ifconfig. GPL-2.0-or-later.
 * One source, built once per tool with -DNET_TOOL="name". The OS supplies the
 * KallistiOS IPv4 stack through the optional net_* native API callbacks. */
#include "app.h"
#include "dreamcast/net.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef NET_TOOL
#define NET_TOOL "ifconfig"
#endif
#define HAS(member) (dc_os->size >= offsetof(struct dc_native_api, member) + sizeof(dc_os->member) && dc_os->member)

static int usage(void)
{
    static const char *const help[] = {
        "ping [-c COUNT] [-s BYTES] [-w MS] HOST (COUNT 1..1000, default 4; BYTES 0..1400, default 56; MS 100..30000, default 2000)",
        "nslookup HOST (IPv4 address lookup via the DHCP-provided DNS server)",
        "ifconfig (Broadband Adapter status, IPv4 addresses, gateway and DNS)",
        NULL
    };
    for (int i = 0; help[i]; i++)
        if (!strncmp(help[i], NET_TOOL, strlen(NET_TOOL)) && help[i][strlen(NET_TOOL)] == ' ')
            puts(help[i]);
    puts("Needs a Broadband Adapter and a DHCP network; Ctrl+C interrupts.");
    return 0;
}
static int unavailable(void)
{
    fputs(NET_TOOL ": networking is not available in this OS build\n", stderr);
    return 2;
}
static int parse_number(const char *s, unsigned lo, unsigned hi, unsigned *out)
{
    char *end;
    errno = 0;
    unsigned long n = strtoul(s, &end, 10);
    if (!*s || !isdigit((unsigned char)*s) || *end || errno || n < lo || n > hi) return 0;
    *out = (unsigned)n;
    return 1;
}
/* Strict dotted quad: four decimal fields 0..255, no leading zeros or signs. */
static int parse_ipv4(const char *s, uint8_t ip[4])
{
    for (int i = 0; i < 4; i++) {
        if (!isdigit((unsigned char)*s)) return 0;
        unsigned v = 0, digits = 0;
        const char *start = s;
        while (isdigit((unsigned char)*s)) { v = v * 10 + (*s++ - '0'); if (++digits > 3) return 0; }
        if (v > 255 || (digits > 1 && *start == '0')) return 0;
        ip[i] = (uint8_t)v;
        if (i < 3 && *s++ != '.') return 0;
    }
    return !*s;
}
static int numeric_like(const char *s)
{
    for (; *s; s++) if (!isdigit((unsigned char)*s) && *s != '.') return 0;
    return 1;
}
static int valid_hostname(const char *s)
{
    size_t len = strlen(s), label = 0;
    if (!len || len > 253) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = s[i];
        if (c == '.') { if (!label || s[i - 1] == '-') return 0; label = 0; continue; }
        if (!isalnum(c) && c != '-') return 0;
        if (c == '-' && !label) return 0;
        if (++label > 63) return 0;
    }
    return label || s[len - 1] == '.';
}
static const char *net_error(long rc)
{
    switch (rc) {
    case DC_NET_ERR_DOWN: return "network is down (no configured Broadband Adapter)";
    case DC_NET_ERR_TIMEOUT: return "timed out";
    case DC_NET_ERR_SEND: return "send failed";
    case DC_NET_ERR_NOTFOUND: return "name not found";
    case DC_NET_ERR_SERVER: return "DNS server failure";
    case DC_NET_ERR_BUSY: return "resolver is busy, try again";
    case DC_NET_ERR_BREAK: return "interrupted";
    default: return "invalid request";
    }
}
static void print_ip(const uint8_t *ip) { printf("%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]); }
static int resolve_target(const char *host, uint8_t ip[4])
{
    if (numeric_like(host)) {
        if (parse_ipv4(host, ip)) return 0;
        fprintf(stderr, "%s: %s: invalid IPv4 address\n", NET_TOOL, host);
        return 2;
    }
    if (!valid_hostname(host)) {
        fprintf(stderr, "%s: %s: invalid host name\n", NET_TOOL, host);
        return 2;
    }
    if (!HAS(net_resolve)) return unavailable();
    long rc = dc_os->net_resolve(host, 8000, ip);
    if (rc < 0) {
        fprintf(stderr, "%s: %s: %s\n", NET_TOOL, host, net_error(rc));
        return rc == DC_NET_ERR_BREAK ? 130 : 1;
    }
    return 0;
}
static int get_info(struct dc_net_info *info)
{
    if (!HAS(net_info)) return unavailable();
    if (dc_os->net_info(info, sizeof(*info)) < 0) { fputs(NET_TOOL ": network status unavailable\n", stderr); return 2; }
    return 0;
}
static int ifconfig_main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { usage(); return 2; }
    struct dc_net_info n;
    int rc = get_info(&n);
    if (rc) return rc;
    if (n.state == DC_NET_STARTING) { puts("Network is starting (waiting for DHCP); try again shortly."); return 1; }
    if (n.state == DC_NET_NO_ADAPTER) {
        puts("No network adapter detected. Attach a Broadband Adapter (in Flycast, enable BBA emulation).");
        return 1;
    }
    printf("%s: %s\n", n.name[0] ? n.name : "eth0", n.state == DC_NET_UP ? "UP" : "DOWN (no IPv4 address; DHCP failed?)");
    if (n.description[0]) printf("  Adapter    %s\n", n.description);
    printf("  Ethernet   %02x:%02x:%02x:%02x:%02x:%02x   MTU %lu\n", n.mac[0], n.mac[1], n.mac[2], n.mac[3], n.mac[4], n.mac[5],
           (unsigned long)n.mtu);
    if (n.state == DC_NET_UP) {
        fputs("  IPv4       ", stdout); print_ip(n.ip);
        fputs("   Netmask ", stdout); print_ip(n.netmask);
        fputs("   Broadcast ", stdout); print_ip(n.broadcast);
        fputs("\n  Gateway    ", stdout); print_ip(n.gateway);
        fputs("\n  DNS        ", stdout); print_ip(n.dns);
        putchar('\n');
    }
    printf("  IPv4 packets: %lu sent, %lu received, %lu send failures, %lu bad\n", (unsigned long)n.ip_sent,
           (unsigned long)n.ip_received, (unsigned long)n.ip_send_failed, (unsigned long)n.ip_bad);
    return n.state == DC_NET_UP ? 0 : 1;
}
static int nslookup_main(int argc, char **argv)
{
    if (argc != 2 || argv[1][0] == '-') { usage(); return 2; }
    struct dc_net_info n;
    int rc = get_info(&n);
    if (rc) return rc;
    if (n.state != DC_NET_UP) { fprintf(stderr, "nslookup: %s\n", net_error(DC_NET_ERR_DOWN)); return 1; }
    uint8_t ip[4];
    if (numeric_like(argv[1])) { fputs("nslookup: reverse lookup is not supported\n", stderr); return 2; }
    rc = resolve_target(argv[1], ip);
    if (rc) return rc;
    fputs("Server:  ", stdout); print_ip(n.dns);
    printf("\nName:    %s\nAddress: ", argv[1]); print_ip(ip); putchar('\n');
    return 0;
}
static unsigned long now_ms(void) { return HAS(millis) ? dc_os->millis() : 0; }
/* Sleep politely; report Ctrl+C. Cnecin consumes only the key it inspects. */
static int wait_or_break(unsigned ms)
{
    unsigned long start = now_ms();
    do {
        if (dc_os->gemdos(0x0b) && ((unsigned long)dc_os->gemdos(0x08) & 0xff) == 3) return 1;
        if (dc_os->yield) dc_os->yield();
    } while (now_ms() - start < ms);
    return 0;
}
static int ping_main(int argc, char **argv)
{
    unsigned count = 4, size = 56, wait = 2000;
    int arg = 1;
    for (; arg < argc && argv[arg][0] == '-' && argv[arg][1]; arg++) {
        if (!strcmp(argv[arg], "--")) { arg++; break; }
        unsigned lo = 1, hi = 1000, *value = &count;
        if (!strcmp(argv[arg], "-s")) { lo = 0; hi = 1400; value = &size; }
        else if (!strcmp(argv[arg], "-w")) { lo = 100; hi = 30000; value = &wait; }
        else if (strcmp(argv[arg], "-c")) { usage(); return 2; }
        if (arg + 1 >= argc || !parse_number(argv[arg + 1], lo, hi, value)) { usage(); return 2; }
        arg++;
    }
    if (argc - arg != 1) { usage(); return 2; }
    const char *host = argv[arg];
    if (!HAS(net_ping)) return unavailable();
    struct dc_net_info n;
    int rc = get_info(&n);
    if (rc) return rc;
    if (n.state != DC_NET_UP) { fprintf(stderr, "ping: %s\n", net_error(DC_NET_ERR_DOWN)); return 1; }
    uint8_t ip[4];
    rc = resolve_target(host, ip);
    if (rc) return rc;
    fputs("PING ", stdout); fputs(host, stdout); fputs(" (", stdout); print_ip(ip);
    printf("): %u data bytes\n", size);
    unsigned sent = 0, received = 0;
    unsigned long total = 0, best = 0xffffffffUL, worst = 0;
    int interrupted = 0;
    for (unsigned seq = 1; seq <= count && !interrupted; seq++) {
        struct dc_ping_result r;
        unsigned long began = now_ms();
        long result = dc_os->net_ping(ip, seq, size, wait, &r, sizeof(r));
        sent += result != DC_NET_ERR_BREAK;
        if (result == DC_NET_OK) {
            received++;
            total += r.rtt_us; if (r.rtt_us < best) best = r.rtt_us; if (r.rtt_us > worst) worst = r.rtt_us;
            printf("%lu bytes from ", (unsigned long)r.reply_bytes + 8); print_ip(r.from);
            printf(": icmp_seq=%u ttl=%lu time=%lu.%03lu ms\n", seq, (unsigned long)r.ttl,
                   (unsigned long)r.rtt_us / 1000, (unsigned long)r.rtt_us % 1000);
        } else if (result == DC_NET_ERR_BREAK) interrupted = 1;
        else if (result == DC_NET_ERR_TIMEOUT) printf("Request timeout for icmp_seq %u\n", seq);
        else { printf("icmp_seq=%u: %s\n", seq, net_error(result)); if (result == DC_NET_ERR_DOWN) break; }
        if (seq < count && !interrupted) {
            unsigned long spent = now_ms() - began;
            if (spent < 1000) interrupted = wait_or_break((unsigned)(1000 - spent));
        }
    }
    printf("\n--- %s ping statistics ---\n%u packets transmitted, %u received, %u%% packet loss\n", host, sent, received,
           sent ? (sent - received) * 100 / sent : 0);
    if (received) printf("round-trip min/avg/max = %lu.%03lu/%lu.%03lu/%lu.%03lu ms\n", best / 1000, best % 1000,
                         total / received / 1000, total / received % 1000, worst / 1000, worst % 1000);
    return interrupted ? 130 : received ? 0 : 1;
}
int app_main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--help")) return usage();
    if (!strcmp(NET_TOOL, "ping")) return ping_main(argc, argv);
    if (!strcmp(NET_TOOL, "nslookup")) return nslookup_main(argc, argv);
    return ifconfig_main(argc, argv);
}
