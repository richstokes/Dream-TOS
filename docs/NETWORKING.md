# Networking

The OS drives the Dreamcast Broadband Adapter (HIT-0400) and LAN Adapter
(HIT-0300) with KallistiOS's IPv4 stack. A background thread registers the
adapters and requests a DHCP lease at boot, so the desktop never waits for a
network. The serial port is left alone: the W5500 serial adapter probe that
`INIT_NET` would run is deliberately skipped, because that port is used for
SD cards.

This is a native ABI service, not a port of STiNG. STiNG is a 68000 driver
with a Motorola-specific API, and 68000 binaries cannot run here. Applications
instead use three optional callbacks in `struct dc_native_api`
(`net_info`, `net_ping`, `net_resolve`; see [NATIVE-ABI](NATIVE-ABI.md)).

## Tools

Run from EmuCON (File → Execute EmuCON, or Ctrl+Z):

```text
ifconfig
ping 192.168.169.1
ping -c 10 -s 128 example.com
nslookup example.com
```

`ifconfig` reports "starting" while DHCP is in progress (up to about a minute
when no server answers), "no adapter" when none is found, or the adapter's
addresses. `ping` needs a configured adapter; names are resolved first through
the DNS server the DHCP lease supplied. Ctrl+C stops it with a summary.
Reverse lookups, TCP/UDP sockets, IPv6 and static configuration are not
exposed to applications yet.

DNS uses `src/dreamcast/dns.c`, not KOS `getaddrinfo`: the KOS resolver does
not check the transaction ID, and a late duplicate reply to an earlier query
was observed being accepted as the answer to the next name. The replacement
accepts only replies that match both ID and question.

## Running in Flycast

`scripts/run-flycast.sh` enables Flycast's built-in BBA emulation
(`network:EmulateBBA=yes`, `network:DCNet=no`): PicoTCP supplies DHCP
(192.168.169.2, gateway .1), NAT and DNS through the host. Set `FLYCAST_BBA=0`
to boot without an adapter. Flycast's default DCNet mode needs its online
service and does not lease an address offline. Ping replies from external
addresses may come from the emulator's stack rather than the remote host.
On hardware the tools work against a real DHCP network; this has not been
tested on a console.

## Testing

`tests/test_net.py` runs the real tool sources under ASan/UBSan against a
scripted OS and unit-tests the DNS packet code, including stale-reply
rejection. In Flycast (BBA emulated), boot-time EmuCON scripts confirmed the
lease, gateway ping, name pings and successful and failing lookups.
