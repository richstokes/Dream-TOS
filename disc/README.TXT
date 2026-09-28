EmuTOS for Dreamcast - native SH-4 development build

C: is a 4 MiB RAM disk. All changes disappear on reset.
D: is the read-only disc volume. Copy files to C: to edit them.

Use a Dreamcast keyboard and mouse on any Maple ports.
Alt+C opens the RAM disk. Alt+D opens this disc.
HELLO.PRG is a separately compiled native SH-4 GEM application.
Run it to create C:\NATIVE.TXT and display a GEM alert.

Atari 68000 programs cannot run on this port. No CPU emulator is used.
Programs must be recompiled for the native SH-4 API.
Serial SD support is not yet implemented.

Source and build instructions:
https://github.com/richstokes/dreamcast-EmuTOS-port
