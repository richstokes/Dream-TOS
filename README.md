# Native EmuTOS for Sega Dreamcast

A work-in-progress SH-4 port of [EmuTOS](https://emutos.sourceforge.io/),
using KallistiOS for Dreamcast hardware support. The intent is to run the
actual EmuTOS GEM desktop, AES, VDI and GEMDOS C code natively.

Atari 68000 `.PRG` executables are not native SH-4 programs and will not run.
Applications must be recompiled/ported for the native ABI. This project does
not contain a 68000 emulator.

Implemented hardware: 640×480 framebuffer, Maple keyboard and mouse, a small
volatile RAM disk, read-only optical storage, CDI packaging is in progress.
Serial SD support is deferred.

Status: native SH-4 ELF boots the actual EmuDesk desktop in Flycast. RAM-disk
create/write/read tests pass; keyboard drive shortcuts and mouse motion work.
Window rendering and GEM call packing have been adapted for little-endian SH-4.
Native application loading and CDI boot verification are still in progress.
Real hardware is untested. Build with `./scripts/build.sh`, then
`./scripts/run-flycast.sh`. Set `KOS_ENV` if the SDK is not installed under
`~/.local/share/dreamcast/kos`. See
[provenance](docs/UPSTREAM.md). EmuTOS is GPL; see COPYING.
