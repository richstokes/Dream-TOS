# Native Dreamcast architecture

KallistiOS owns startup, SH-4 exceptions, IRQs, thread contexts, cache operations,
Maple devices, timers, video mode setup and ISO9660 disc reads. The OS itself
runs natively; Flycast emulates the Dreamcast only for development.

The upstream AES, EmuDesk, VDI C primitives, FAT filesystem, resource builders,
fonts and VT52 console are compiled for SH-4. Legacy assembly entry points are
replaced with C adapters in `src/dreamcast`. Motorola assembly is not linked.
AES processes use KOS thread stacks with semaphore gates: only one AES process
executes GEM at a time, preserving the original cooperative scheduling model.

The graphics backend maintains a 640×480 four-plane buffer. Native C rendering
updates it; the HAL converts changed frames to RGB565 and copies them to video
RAM using SH-4 store queues. Font data and planar pixels remain numeric 16-bit
words. The native glyph renderer handles scaling, styling and rotation using numeric
font words, with host sanitizer tests and a loadable VDI test application.
Byte-order adaptations cover console cells, FAT disk fields, AES event
packing, object specs and mouse save/restore buffers.

Maple mouse input is collected in a separate KOS thread so short presses can
survive expensive GEM redraws. Relative movement is consumed once and queued
with button transitions. Maple HID key codes become Atari-compatible scancodes
and region-specific characters. The physical keyboard/mouse can be on any
Maple port; the first matching device is used.

GEMDOS retains its FAT implementation. C: is a formatted 4 MiB allocation;
D: exposes the same fixed geometry from `/cd/DISC.IMG`. Writes to D: are denied
both in the GEMDOS adapter and at the block-device layer. Writable storage
can later be added behind this block interface, including a serial SD driver.
GEMDOS has a separate 3 MiB allocation arena with ownership and Mshrink support;
KOS allocations, stacks and program images are separate.

The native ABI replaces stack decoding of 68000 traps with typed C dispatch.
The ROM desktop returns directly to the AES shell when launching a program.
Applications use relocatable SH-4 containers and an explicit OS function table.
See NATIVE-ABI.md for the file format and limits.
