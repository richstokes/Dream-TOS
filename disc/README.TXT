Dream TOS for Sega Dreamcast - native SH-4 development build

C: is a 4 MiB RAM disk. All changes disappear on reset.
D: is the read-only disc volume. Copy files to C: to edit them.

Use a Dreamcast keyboard and mouse on any Maple ports.
Alt+C opens the RAM disk. Alt+D opens this disc.
Open APPS for everyday programs, GAMES for games, UTILS for tools.
All program folders are on the EmuCON search path (Ctrl+Z).
APPS.TXT has the full program list and controls; CLI.TXT covers commands.
Desk > Calculator opens the resident calculator beside other windows.
Close or Esc hides it; reopen from Desk to keep the expression and answer.

UTILS\HELLO.PRG is a separately compiled native SH-4 GEM application.
Run it to create C:\NATIVE.TXT and display a GEM alert.

UTILS\VDITEST.PRG exercises native text, drawing, clipping and bitmap copies.
It should report PASS. Press Return to leave it.
Alt+arrows moves the pointer; Alt+Space clicks; Ctrl+O opens selection.

Atari 68000 programs cannot run on this port. No CPU emulator is used.
Programs must be recompiled for the native SH-4 API.
Serial SD cards with supported FAT16 volumes appear as E: through H:.

Source and build instructions:
https://github.com/richstokes/Dream-TOS
