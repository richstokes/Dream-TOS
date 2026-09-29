# Native Dreamcast architecture

KallistiOS owns startup, SH-4 exceptions, IRQs, thread contexts, cache operations,
Maple devices, timers, video mode setup and ISO9660 disc reads. The OS itself
runs natively; Flycast emulates the Dreamcast only for development.

The upstream AES, EmuDesk, VDI C primitives, FAT filesystem, resource builders,
fonts, EmuCON2 shell and VT52 console are compiled for SH-4. Legacy assembly entry points are
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
D: exposes the same fixed geometry from `/cd/DISC.IMG`. CD reads pass through
a consistent 32-byte-aligned transfer buffer so KOS does not mix its streaming
and cached paths when GEMDOS supplies differently aligned buffers. Writes to D: are denied
both in the GEMDOS adapter and at the block-device layer. An SD card on the serial port is scanned at boot and its FAT12/FAT16
partitions are exposed as writable E: to H: through the same block interface;
see [SD card support](SD.md).
GEMDOS has a separate 3 MiB allocation arena with ownership and Mshrink support;
KOS allocations, stacks and program images are separate.

Control Panel preferences use a separate, named two-block VMU save with an icon
through KOS. They load at accessory startup and write only on an explicit Save action;
the VMU is not exposed as a writable GEMDOS drive.

Native programs can also manage VMU files through a small, validated API
(`vmu_file_read/write/delete`, `vmu_screen`): `src/dreamcast/vmu_file.c` checks
the card, names, protection and space, then KOS `vmufs` makes the one file-level
change and the engine reads everything back. Nothing can format a card. See
[NATIVE-ABI.md](NATIVE-ABI.md); `VMUEDIT.PRG` is its user interface.

File → Boot GD-ROM… confirms a restart, then uses KOS's `ARCH_EXIT_REBOOT`
exit path to shut down drivers and enter the Dreamcast boot ROM. The BIOS boots
the inserted disc as usual; leaving the EmuTOS disc inserted boots EmuTOS again.
C: files and settings not saved to VMU are lost. This does not select a GDEMU
image or bypass BIOS disc compatibility checks.

The native ABI replaces stack decoding of 68000 traps with typed C dispatch.
The ROM desktop returns directly to the AES shell when launching a program.
Applications use relocatable SH-4 containers and an explicit OS function table.
See NATIVE-ABI.md for the file format and limits.

EmuCON enters through a native C wrapper with a private DTA and the AES PATH
environment. Its native child programs use the existing foreground Pexec
slot, so this does not add nested Pexec for applications. GEMDOS standard
handles support Fdup/Fforce and console/file I/O. The newlib runtime preserves
stdout/stderr redirection; EmuCON releases its history buffer on return.
See [COMMAND-LINE.md](COMMAND-LINE.md) for the bundled utilities and limits.
