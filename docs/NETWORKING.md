# Networking

The OS drives the Dreamcast Broadband Adapter (HIT-0400) and LAN Adapter
(HIT-0300) with KallistiOS's IPv4 stack. A background thread registers the
adapters and requests a DHCP lease at boot, so the desktop never waits for a
network. The serial port is left alone: the W5500 serial adapter probe that
`INIT_NET` would run is deliberately skipped, because that port is used for
SD cards.

Applications access networking through optional callbacks in `struct
dc_native_api` for status, ping, DNS and non-blocking TCP listeners and outbound connections; see
[NATIVE-ABI](NATIVE-ABI.md). KallistiOS supplies the shared IP, TCP and UDP
implementation underneath these calls. The FTP protocol engine and DNS reply
validation are application/service code above that stack.

## Stack choice and future applications

Decision, 2026-10-03: retain **KallistiOS as the single network stack** and
extend the native application interface as new programs need more operations.
The current integrations already share the adapter, DHCP lease and IP stack:

| Application | Native service | KallistiOS backend |
|---|---|---|
| `SSH.TTP` | `net_info`, `net_resolve`, `tcp_*` | Outbound TCP with wolfSSH/wolfCrypt above the shared stack |
| `FTP.PRG` | `net_info`, `tcp_*` | Interface snapshot and TCP sockets |
| `PING.TTP` | `net_info`, `net_resolve`, `net_ping` | Interface, UDP DNS queries and ICMP |
| `NSLOOKUP.TTP` | `net_info`, `net_resolve` | Interface and UDP DNS queries |
| `IFCONFIG.TTP` | `net_info` | Interface addresses, adapter and IPv4 counters |

The TCP boundary in `src/dreamcast/hal_tcp.c` is a small adapter to KOS socket
calls. New apps can reuse it; they do not need their own transport stack.
The KOS SDK already provides socket operations for outbound connections and
datagrams, plus polling; these are not all exported through our native API
yet. See the pinned SDK's [socket interface](https://github.com/KallistiOS/KallistiOS/blob/cd340378043f00cd1d05284ee0d02548d33d6054/include/sys/socket.h)
and [poll interface](https://github.com/KallistiOS/KallistiOS/blob/cd340378043f00cd1d05284ee0d02548d33d6054/include/poll.h).

For future app work:

- Reuse `tcp_connect` / `tcp_connected` for new clients such as HTTP or IRC.
  These preserve partial I/O, EOF and retry semantics, as exercised by SSH.
- Add UDP send/receive with peer addresses and readiness polling when a
  datagram-based app needs them. Expose asynchronous DNS completion for GUI
  clients; the current `net_resolve` call waits while servicing OS input.
- Keep SDK socket structures and descriptors inside the normally aligned HAL.
  Extend the versioned native function table by appending callbacks; callers
  check both size and pointer. A BSD-style source adapter can sit above this
  boundary when porting software written for `socket`/`connect`/`poll`.
- Before supporting resident network accessories or concurrent app owners,
  add owner-scoped socket lifetime and cleanup. The present eight-handle TCP
  pool is for the single foreground program and is cleared at its termination.
- Integrate a TLS library above TCP when an app needs HTTPS or another secure
  protocol. TLS is a separate application requirement, not provided by the
  current native socket API.

These are extension points, not additional features implemented by the FTP
change. Existing tools continue to use the same KOS-backed services.

### STinG assessment

Reviewed [th-otto/STinG](https://github.com/th-otto/STinG/tree/ab065dc94b01e9a5e29b584e2566a6552facd504).
Its [README](https://github.com/th-otto/STinG/blob/ab065dc94b01e9a5e29b584e2566a6552facd504/README.md)
describes the repository as primarily historical, with no major development
planned, and says its GCC build does not produce working executables because
assembly interfaces still expect Pure-C register conventions. Its
[transport header](https://github.com/th-otto/STinG/blob/ab065dc94b01e9a5e29b584e2566a6552facd504/include/transprt.h)
also contains explicit 68000 register/assembly wrappers and identity byte-order
conversion macros. A native little-endian SH-4 port would need to address
these assumptions and integrate Dreamcast hardware and OS services.

That work would duplicate transport functionality already supplied by KOS.
It would not make Atari networking binaries executable: this port still needs
every app rebuilt for SH-4. STinG's useful compatibility opportunity is its
application interface. If a chosen source port relies heavily on that API,
implement and test the needed calls as a source-level adapter over the shared
KOS-backed services. Such an adapter would need to match STinG's connection,
error and buffer-ownership semantics; none is implemented today.

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
Reverse lookups, UDP sockets, IPv6 and static
configuration are not exposed to applications yet.

**D:\UTILS\FTP.PRG** is a [graphical anonymous FTP server](FTP.md). Choose a drive or
folder (including mounted SD volumes), then Start. It uses the same stack via
the non-blocking TCP native API, with passive connections on port 21.

**D:\UTILS\SSH.TTP** is an [SSH remote terminal](SSH.md), supporting password,
interactive and private-key login with pinned host fingerprints. It uses the
same TCP adapter; no second TCP/IP stack is introduced.

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
