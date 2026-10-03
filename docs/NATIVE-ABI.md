# Dreamcast native application ABI, version 1

Applications are SH-4 little-endian code using GCC `-m4-single -ml`.
Integers, longs and pointers are 32 bits; GEM WORDs are 16 bits. Compile GEM
structures with two-byte packing. Minimal freestanding apps may use
`-fpack-struct=2`; apps using newlib must instead pack only GEM structures
with `#pragma pack(push,2)` / `#pragma pack(pop)`. Portable C and newlib
structures use normal alignment; do not mix the two layouts.
The exported `dc_native_api` table always has at least four-byte alignment,
including when the OS or an application is built with `-fpack-struct=2`.
Pointers embedded at GEM WORD offsets must be accessed with alignment-safe
loads/stores; casting those positions to ordinary pointer pointers can fault
on the console's SH-4.

The entry point receives `const struct dc_native_api *`, a length-prefixed
GEMDOS command tail and a double-NUL-terminated environment. It returns a
GEMDOS exit status. The API definition is `include/dreamcast/native.h`:

- `gemdos(opcode, ...)`: native C call with default argument promotions. Pass
  WORD arguments as `int`, LONG arguments as `long`, and addresses as pointers.
- `aes(pb)` and `vdi(pb)`: native pointers to GEM parameter blocks. Array
  elements retain GEM WORD widths. Addresses in address arrays are 32 bits.
- `yield()`: service display, timers and input during long computations.
- Optional `millis()`: monotonic milliseconds for game timing. Check
  `size >= offsetof(struct dc_native_api, millis) + sizeof(api->millis)`
  before using this extension. The v1 prefix and program format are unchanged.

Additional optional v1 callbacks are appended to the table. Check both the
API `size` through the requested member and its non-null pointer; the shared
accessory runtime provides `APP_HAS(member)`. Their fixed-width snapshot
structures have a `version` and `bytes` header and work with both the native
and GEM packing boundaries.

- `system_info(buffer, bytes)`: system memory, uptime, drives and Maple devices;
  see `include/dreamcast/system_info.h`.
- `input_config(write, buffer, bytes)`: get (`write=0`) or atomically validate
  and apply (`write=1`) session settings. Mouse speed is 25–400 percent;
  repeat delay 100–1000 ms and repeat interval 20–200 ms. A write requires
  the current structure version and exact structure `bytes` field.
- `input_snapshot(buffer, bytes)`: cached input state without consuming keys
  or mouse events; see `include/dreamcast/control.h` for both input structures.
- `vmu_info(port, unit, buffer, bytes)`: bounded, read-only directory snapshot;
  see `include/dreamcast/vmu_info.h`. Only root, FAT and directory blocks are
  read, using KOS `vmu_block_read`. This interface cannot write to a card;
  file writes use the separate `vmu_file_*` calls below.
- `control_store(write, port, unit, buffer, bytes)`: load (`write=0`) or save
  (`write=1`) only `EMUTOS.CFG` on the specified VMU. The structure in
  `include/dreamcast/settings.h` contains input settings and the desktop colour.
  Returns the structure size on success, `-64` for invalid arguments, or a
  `DC_SETTINGS_*` error. Failed loads leave the buffer untouched. This does not
  apply settings; the panel applies validated input and palette values itself.
  Apart from the VMU file service below there is no VMU write API, and nothing
  can format a card.

A null buffer with zero size queries the required structure size (input
configuration and control-store queries use `write=0`). Successful calls return
that size; invalid arguments return `-64` without modifying the caller's buffer. VMU
inspection errors instead return a complete snapshot with a negative
`status` and zero counts. The reader supports standard 128 KiB layouts
with one FAT block and up to 16 directory blocks; it checks bounds, file
chains, cycles and cross-links. It does not read save payloads or titles
from VMS headers. Calls can take time for Maple I/O, so refresh VMUs on
explicit user actions, not on a periodic timer.

- `vmu_file_read(port, unit, name, buffer, bytes)`,
  `vmu_file_write(port, unit, name, data, bytes, flags)`,
  `vmu_file_delete(port, unit, name)` and `vmu_screen(port, unit, bitmap, bytes)`:
  the VMU file service and LCD (in the API table they follow the network calls,
  in a block of their own; check `size` for each member, e.g. `APP_HAS(vmu_file_write)`). See
  `include/dreamcast/vmu_file.h` for the error codes (`DC_VMUF_*`). All results
  are negative on failure and never partially reported: each error says whether
  the card was left unchanged.
  - `vmu_file_read` returns the size (blocks x 512) and fills `buffer`; a NULL
    buffer with `bytes==0` only returns the size. Copy-protected files are
    refused (`DC_VMUF_PROTECTED`).
  - `vmu_file_write` creates a file, or with `DC_VMUF_OVERWRITE` replaces one, and
    returns the stored size after a read-back. `bytes` is 1 to 102400 (200
    blocks) and is zero-padded to whole 512-byte blocks. New files are always
    ordinary copyable data files (type 0x33); no games, no copy-protect flag.
  - `vmu_file_delete` deletes one ordinary data file and returns 0.
  - `vmu_screen` draws a 48x32 1-bit bitmap on the VMU LCD: 192 bytes, six per
    row, top row first, most significant bit leftmost, 1 = dark pixel. The call
    rotates it into KOS `vmu_draw_lcd_rotated` order. The LCD is not saved to
    the card. `DC_VMUF_BUSY` means the Maple frame was busy: retry later.
  - Names are 1 to 12 printable ASCII characters (trailing spaces trimmed, no
    leading space). A file whose stored name contains other bytes cannot be
    addressed.

  Safety rules the OS enforces before the first write, in `src/dreamcast/vmu_file.c`:
  the card must pass the same root/FAT/directory validation as `vmu_info`
  (standard 128 KiB layout, at most 200 user blocks, one FAT block; chains
  bounded, acyclic, not cross-linked, exactly the recorded length), or the call
  fails with `DC_VMUF_CARD` without writing. Names are matched exactly as KOS
  `vmufs` matches them (`strncmp` over the 12 stored bytes); duplicate or
  differently padded look-alikes that could make KOS touch the wrong entry are
  refused (`DC_VMUF_AMBIGUOUS`). Existing files are replaced only if they are
  ordinary data files without copy protection and without a header offset; games
  (0xcc), copy-protected and odd files are refused for write and delete.
  Free blocks (an overwrite reuses its own blocks), directory slots and the
  200-block limit are checked up front. The old contents of a file being
  replaced are read first as a rollback copy. The single file-level change is
  made by KOS `vmufs_write`/`vmufs_delete`, so blocks are allocated exactly as
  a standard driver does and only the FAT and directory blocks change: the root
  block is never written, and there is no format call. Afterwards the OS reads
  the directory, FAT accounting, every other file and the new file's bytes back;
  a mismatch triggers a restore of the previous contents where the card is still
  consistent (`DC_VMUF_RESTORED`), else `DC_VMUF_VERIFY`. Limits: KOS writes the
  FAT and directory after the data, so a power loss or card removal during a
  write can leave leaked blocks or, in the worst window, an inconsistent card
  that needs the console file manager; the call is not atomic and is not
  re-entrant (one call at a time; `DC_VMUF_BUSY`). Calls block for many Maple
  transactions (flash writes are slow: a full 200-block file can take many
  seconds, plus its read-back), so call only from an explicit user action and
  tell the user to keep the card connected. These paths were tested on the host against KOS's own
  `vmufs.c` and an in-memory card, not on a real console; see
  [testing](TESTING.md).

- `net_info(buffer, bytes)`, `net_ping(ip, seq, size, timeout_ms, result, bytes)`
  and `net_resolve(host, timeout_ms, ip)`: Broadband Adapter status, one ICMP
  echo, and an IPv4 DNS lookup; see `include/dreamcast/net.h` and
  [networking](NETWORKING.md). Calls wait while servicing input; Ctrl+C returns
  `DC_NET_ERR_BREAK`.

- `tcp_listen(port)`, `tcp_accept(handle, peer_ip)`, `tcp_recv(handle, buffer, bytes)`,
  `tcp_send(handle, buffer, bytes)`, `tcp_port(handle)` and `tcp_close(handle)`:
  non-blocking IPv4 TCP listeners for foreground programs, appended after the
  VMU block. Check size and each pointer. See `include/dreamcast/tcp.h`.
  Handles are positive service-owned tokens, not KOS or GEMDOS descriptors;
  there are eight slots. Port 0 chooses an ephemeral port; `tcp_port` returns
  the actual port in host byte order. Accept fills four network-order IPv4
  bytes. Receive/send return a possibly partial byte count, `DC_TCP_AGAIN`
  (-8) when retry is needed, `DC_TCP_IO` (-9) on failure or -64 for invalid
  arguments. Receive returns zero at EOF; requests must be 1–65536 bytes.
  Close handles explicitly; foreground process termination also closes all
  remaining TCP handles. These handles are not for resident accessories.
  KOS socket structures stay inside `hal_tcp.c`, compiled with normal SDK
  alignment. The FTP app polls these calls from its AES loop and performs all
  GEMDOS file work on that same thread.

- `audio_open(rate, channels)`, `audio_close()`, `audio_write(pcm, frames)`,
  `audio_space()`, `audio_set(what, value)` and `audio_info(buffer, bytes)`:
  AICA sound output for signed 16-bit interleaved PCM, mono or stereo, 8000 to
  48000 Hz; see `include/dreamcast/audio.h` and [audio](AUDIO.md). The
  application decodes and the OS owns the AICA: `audio_write` copies into a
  32768-frame ring and never blocks (it returns the frames taken, possibly
  zero), and a KOS thread feeds the hardware, so sound continues while the
  application decodes or redraws. `audio_set` takes `DC_AUDIO_VOLUME`
  (0-255), `DC_AUDIO_PAUSE` and `DC_AUDIO_FLUSH` (drop queued sound and zero
  the position). `audio_info` reports free/queued frames, the position in
  frames handed to the AICA (sound is heard up to about 0.2 s later) and an
  underrun count. Only one stream can be open (`DC_AUDIO_ERR_BUSY`). The sound
  driver starts on the first `audio_open`, never at boot. The loader closes the
  stream when the program returns or calls Pterm, so a program cannot leave
  the AICA playing. Applications must not call KOS directly. Errors
  are negative: `-64` bad arguments, `-1` no driver, `-2` busy, `-3` not open,
  `-4` out of memory. This block was appended after the networking callbacks.

There are no Motorola traps, register argument conventions or fixed Atari
hardware addresses. Applications must not call KOS using this packed ABI.
Unsupported GEMDOS functions return EINVFN. The initial loader supports
synchronous Pexec mode 0 and termination by return, Pterm0 or Pterm; nested
Pexec, TSRs, signals and process isolation are not implemented. Native desk
accessories are supported as described below.
All native programs are trusted code sharing the OS address space.

`apps/hello.c` is a complete separately linked example. Build it with
`./scripts/build-apps.sh`. It creates C:\NATIVE.TXT, opens a GEM alert and exits.
At boot, a special TEST command tail checks relocation, BSS, GEMDOS and exit
status before AES starts. Normal desktop launch follows the graphical path.

The [application bundle](BUNDLE.md) demonstrates a larger porting path.
`apps/lib/runtime.c` supplies a single-threaded newlib host using GEMDOS
allocation, stdio syscalls, command-tail parsing and Pterm. Implement
`int app_main(int argc, char **argv)` and link through `scripts/build-bundle.sh`.
Use normal SH-4 alignment and freestanding compilation; the latter prevents
compiler builtins from rewriting allocator implementations into themselves.
SH-4 RAM pointers commonly have their high bit set: a signed-negative pointer
is not an allocation error. GEMDOS Malloc returns NULL on allocation failure.
Thread-local storage, threads and dynamic shared libraries are unsupported.

`RUNTIME.PRG` runs an automated non-GUI check at boot when the bundle is on
D:, then can be opened interactively as a diagnostic. It checks newlib memory,
stdio, file positioning, read-only enforcement, double-precision math and
post-Pterm memory reclamation.

## Container

A native `.PRG` contains a 32-byte header, initialized image bytes and a table
of little-endian 32-bit relocation offsets. Header fields are:

| Offset | Meaning |
| --- | --- |
| 0 | Eight literal bytes `DCNATIVE` |
| 8 | ABI version, uint32, currently 1 |
| 12 | Initialized image size |
| 16 | Total memory size, including zeroed BSS |
| 20 | Entry offset relative to allocation base |
| 24 | Relocation count |
| 28 | Reserved, must be zero |

`apps/native.ld` links at zero. `tools/native_app.py` consumes a SuperH ELF
built with `--emit-relocs`; resolved R_SH_DIR32 references become relocations.
The loader checks sizes, entry alignment, sorted relocation locations and
pointer bounds before adding the allocation base. It flushes the SH-4 data
and instruction caches before calling the entry point. The current per-program
image/memory limit is 2 MiB. Foreign Atari/ELF files are rejected.

Child file handles and current directories are inherited; owned handles and
GEMDOS allocations are reclaimed on normal termination. The loader restores
the parent process and frees the program allocation before returning to AES.
A CPU exception is currently fatal to the session; this is not a protected OS.

## Native desk accessories

`CLOCK.ACC` demonstrates a resident native GEM accessory. The build uses the
same `DCNATIVE` image format, relocation checks and SH-4 API as `.PRG` files.
At boot AES scans `D:\*.ACC`, loads each validated image, and starts it on a
separate native cooperative AES context. It does not execute 68000 startup
code. The accessory calls `appl_init`, opens a virtual VDI workstation,
registers a Desk-menu entry with `menu_register`, then waits in `evnt_multi`.

`AC_OPEN` opens or tops its window; `WM_CLOSED` hides it without `appl_exit` or
`Pterm`. `AC_CLOSE` invalidates its window handle because the shell resets
windows when switching the foreground program. Keep persistent allocations
in initialization, before the first message wait: classic TOS accessories
share the foreground GEMDOS process, whose later allocations are reclaimed
at program exit. The bundled accessories perform no later application heap allocations.
A hidden clock waits only for messages; a visible clock also uses a timer
and paints through the window manager's visible rectangles.

Foreground `Pexec` remains single-tasking. Accessory termination has its own
SH-4 jump target and cannot terminate the foreground program. A returning
accessory is parked in a message wait so that it cannot strand the scheduler.
Accessories are loaded at boot, not by double-clicking their `.ACC` file.

`CONTROL.ACC`, `MONITOR.ACC` and `VMUTOOL.ACC` use `apps/lib/accessory.c` for
this lifecycle, visible-rectangle painting, keyboard window movement and
release-triggered buttons. Hidden accessories wait only for AES messages.
The control panel samples cached input at 200 ms; the monitor samples system
state at one second. VMU Toolbox's one-second timer enumerates devices only;
opening, switching cards and explicit refresh read metadata. It uses a static
snapshot buffer so foreground process cleanup cannot invalidate it.

Control Panel loads the first valid VMU settings save during accessory
initialization. Its timer only enumerates devices and samples cached input;
file reads after initialization and all writes require explicit Load/Save.
The settings service validates card metadata, VMS ownership/header/CRC and
versioned values. A save includes one 32×32 4-bit icon with an ARGB4444 palette
and uses two fully padded 512-byte blocks through KOS, then reads it back for
verification. FAT chains are followed even when the blocks are nonadjacent.
The original iconless one-block format still loads; an explicit Save upgrades
it to the icon format even when the settings are unchanged. Writes are not
power-loss atomic.

## Console programs

Use the same `app_main(argc, argv)` runtime for native `.TOS`/`.TTP` tools.
Standard streams inherit GEMDOS handles; `Fdup` (0x45) and `Fforce` (0x46)
allow stdout and stderr to be redirected separately. Runtime stdio uses
`Fread`/`Fwrite` even for standard descriptors. Only output to the console
converts LF to CR/LF; redirected files retain the original bytes. Console
input is line-oriented with echo, Backspace and Ctrl+D/Ctrl+Z EOF. Ctrl+C
terminates the active foreground console application during input/output.
Command tails support up to 30 arguments with quoted fields.
