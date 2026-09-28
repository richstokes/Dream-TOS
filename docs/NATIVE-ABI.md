# Dreamcast native application ABI, version 1

Applications are SH-4 little-endian code using GCC `-m4-single -ml`.
Integers, longs and pointers are 32 bits; GEM WORDs are 16 bits. Compile GEM
structures with two-byte packing (`-fpack-struct=2`). KallistiOS structures use
normal SDK alignment; do not mix the two layouts.

The entry point receives `const struct dc_native_api *`, a length-prefixed
GEMDOS command tail and a double-NUL-terminated environment. It returns a
GEMDOS exit status. The API definition is `include/dreamcast/native.h`:

- `gemdos(opcode, ...)`: native C call with default argument promotions. Pass
  WORD arguments as `int`, LONG arguments as `long`, and addresses as pointers.
- `aes(pb)` and `vdi(pb)`: native pointers to GEM parameter blocks. Array
  elements retain GEM WORD widths. Addresses in address arrays are 32 bits.
- `yield()`: service display, timers and input during long computations.

There are no Motorola traps, register argument conventions or fixed Atari
hardware addresses. Applications must not call KOS using this packed ABI.
Unsupported GEMDOS functions return EINVFN. The initial loader supports
synchronous Pexec mode 0 and termination by return, Pterm0 or Pterm; nested
Pexec, TSRs, accessories, signals and process isolation are not implemented.
All native programs are trusted code sharing the OS address space.

`apps/hello.c` is a complete separately linked example. Build it with
`./scripts/build-apps.sh`. It creates C:\NATIVE.TXT, opens a GEM alert and exits.
At boot, a special TEST command tail checks relocation, BSS, GEMDOS and exit
status before AES starts. Normal desktop launch follows the graphical path.

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
