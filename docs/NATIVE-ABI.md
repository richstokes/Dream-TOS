# Dreamcast native application ABI, version 1

Applications are SH-4 little-endian code using GCC `-m4-single -ml`.
Integers, longs and pointers are 32 bits; GEM WORDs are 16 bits. Compile GEM
structures with two-byte packing. Minimal freestanding apps may use
`-fpack-struct=2`; apps using newlib must instead pack only GEM structures
with `#pragma pack(push,2)` / `#pragma pack(pop)`. Portable C and newlib
structures use normal alignment; do not mix the two layouts.

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
at program exit. The clock performs no later application heap allocations.
A hidden clock waits only for messages; a visible clock also uses a timer
and paints through the window manager's visible rectangles.

Foreground `Pexec` remains single-tasking. Accessory termination has its own
SH-4 jump target and cannot terminate the foreground program. A returning
accessory is parked in a message wait so that it cannot strand the scheduler.
Accessories are loaded at boot, not by double-clicking their `.ACC` file.
