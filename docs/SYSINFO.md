# Native system information

`SYSINFO.PRG` is a small GPL-2.0-or-later GEM utility written for this port.
The normal application/CDI build includes it at `D:\UTILS\SYSINFO.PRG`. It is native SH-4 code
and needs neither an Atari executable nor a 68000 emulator.

Open it from EmuDesk. All controls work with the keyboard:

- **R** takes a fresh snapshot, including newly attached Maple devices.
- **Space**, **Right/Down** or **Page Down** advances a page.
- **Left/Up** or **Page Up** goes back a page.
- **S** writes the displayed snapshot to `SYSINFO.TXT` on a writable volume,
  preferring a RAM-backed volume. With the current driver setup this is
  `C:\SYSINFO.TXT`; saving again replaces that file. RAM-disk reports disappear
  at reset, so copy or photograph anything needed before rebooting.
- **Escape**, **Return** or **Q** closes the program and restores EmuDesk.

## Where the numbers come from

| Information | Source |
| --- | --- |
| Dream TOS and KOS versions | Running OS build string and linked KOS runtime version |
| Machine type | KOS `hardware_sys_mode()` |
| Installed system RAM | KOS `HW_MEMSIZE`, based on startup RAM mirror detection |
| KOS heap allocation | `mallinfo().uordblks` |
| Uptime | KOS monotonic timer |
| GEMDOS arena capacity | Actual allocator configuration |
| Free GEMDOS memory and largest free block | Walk of the allocator's current block list |
| GEM screen size, colours and bitplanes | VDI workstation queries |
| Output framebuffer format and dimensions | KOS's current video mode |
| Connected cable | KOS `vid_check_cable()` |
| Mounted/write-protected/RAM-backed volumes | Running storage driver state and write policy |
| Volume capacity and free space | GEMDOS `Dfree` for each mounted drive |
| Maple products, port/unit and capabilities | KOS's currently enumerated device descriptors |

The GEMDOS arena is a configured part of system RAM, not additional physical
RAM. Its free memory includes only allocation payloads; allocator bookkeeping
uses some of the arena. KOS heap allocation includes OS-managed allocations
such as the RAM disk and arena. Volume capacity is usable filesystem data
capacity, so it is smaller than the raw image size. Displaying a Maple storage
device does not imply that EmuTOS can mount it.

CPU clock speed and VRAM capacity are deliberately left unreported: the query
does not measure them. The SH-4 label describes the native application ABI.
Inside Flycast the values describe its emulated Dreamcast, not the host Mac.

## Query interface

`include/dreamcast/native.h` appends an optional `system_info(buffer, bytes)`
function to the native API v1 table, after `millis`. Existing entries and
their offsets are unchanged. Before accessing it, check that `api->size` is
at least `offsetof(struct dc_native_api, system_info) +
sizeof(api->system_info)`, then check that the function pointer is non-null.

The snapshot layout is in `include/dreamcast/system_info.h`. It contains
fixed-width 32-bit scalars and inline strings, without KOS structs or pointers.
The layout is identical with normal and GEM two-byte packing. `NULL, 0`
queries its required size. A null pointer with a nonzero size or an undersized
buffer returns -64 without writing. Success fills the v1 structure and returns
its size (1180 bytes). The version and byte-count fields must also be checked.
An older OS without the extension gets an explanatory screen in SYSINFO.

Maple descriptors are copied under KOS's IRQ gate to prevent a device detach
from invalidating a pointer while it is being copied. Names are bounded,
sanitized and NUL-terminated. Querying does not probe by writing to devices,
change drivers, or initiate additional Maple transactions.

## Checks

The app and OS compile with the pinned SH-4 SDK. `tests/test_sysinfo.py`
compiles a host integration harness with AddressSanitizer and UndefinedBehaviorSanitizer.
It supplies different screen sizes, memory capacities, drive letters and device
names, checks that the report reflects those values, and exercises report
saving, disk-write failure, no writable volume, absent devices and an older
native API table.

For the initial implementation, an isolated OS/app build and CDI were created
under `build/sysinfo-check/`. Interactive Flycast and real-hardware checks are
still pending; the main application's active Flycast session was left alone.
