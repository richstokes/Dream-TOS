# Native EmuTOS for Sega Dreamcast

A work-in-progress SH-4 port of [EmuTOS](https://emutos.sourceforge.io/),
using KallistiOS for Dreamcast hardware support. The intent is to run the
actual EmuTOS GEM desktop, AES, VDI and GEMDOS C code natively.

Atari 68000 `.PRG` executables are not native SH-4 programs and will not run.
Applications must be recompiled/ported for the native ABI. This project does
not contain a 68000 emulator.

Planned hardware: 640×480 framebuffer, Maple keyboard and mouse, a small
volatile RAM disk, read-only optical storage, self-booting CDI images.
Serial SD support is deferred.

Status: upstream imported; architecture port and build in progress. No image
has yet been verified in Flycast or on real hardware. See
[provenance](docs/UPSTREAM.md). EmuTOS is GPL; see COPYING.
