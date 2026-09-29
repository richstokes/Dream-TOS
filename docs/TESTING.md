# Testing and current limits

## Verified in Flycast

2026-09-28, native SH-4 ELF and self-boot CDI, KOS 2.3.0 / GCC 15.2.0:

- Boots into the real upstream EmuDesk with C: and D: icons.
- Alt+D opens the optical volume; directory and file sizes are correct.
- Keyboard pointer selection and Ctrl+O launch D:\HELLO.PRG. Its AES alert
  appears, it writes C:\NATIVE.TXT, and Return returns to the desktop.
- Opening D:\README.TXT and choosing Show renders the complete VT52 document;
  Escape returns to the desktop.
- Alt+C, Ctrl+N, typing KEYTEST and Return creates a RAM-disk directory.
- Boot checks exercise multi-cluster write/read, seek, rename, deletion,
  free-space recovery, read-only rejection, Mshrink, native relocation, BSS
  initialization and application exit status. Failures stop boot with a log.
- `python3 -m unittest discover -s tests -v` checks FAT chains, mirrored tables,
  empty files, file names and the native program container. Build apps first.
- D:\VDITEST.PRG opens a virtual workstation and displays scaled, styled and
  rotated text, palette bars, polylines and a filled circle. Pixel readback
  verifies circle fill, XOR restoration, clipping and a bitmap copy whose
  destination is not aligned to a 16-pixel word. Return restores EmuDesk.
- Native application Pterm and process-owned memory cleanup pass at boot.
- Sanitized host glyph tests cover numeric word order, fractional scaling,
  spacing, all four rotations, bold, light, italic and outline.
- Repo-local mkdcdisc bootstrap and CDI packaging succeed on macOS ARM64.

Input tests use Flycast controller A, keyboard B, mouse C. The launch script
sets these options transiently. Real hardware locates devices by Maple type.
A controller supplies pointer movement and A/B buttons if no mouse is present.

The macOS UI automation cannot emit the modifier-only capture shortcut. A
modified-arrow attempt did not visibly enable capture. Absolute host pointer
warps also affect relative guest movement, so captured mouse accuracy and
physical double clicks still need manual testing. Prefer keyboard navigation
for reproducible emulator checks. Do not interpret the separate host pointer
as a guest hit-test location.

The menu-bar clock was verified in Flycast on 2026-09-29: correct placement
with the desktop and D: window visible, and an automatic minute change.
Sanitized host tests cover midnight rollover, unchanged-minute suppression,
menu hide/show, crowded titles, font geometry and restoration of clipping.

## EmuCON validation

Flycast checks on 2026-09-29 verified Ctrl+Z entry, keyboard input through
`head -n 1 -`, EOF through `wc -` and Ctrl+D, Ctrl+C interruption, PATH lookup,
launching `editor C:\WELCOME.TXT`, return to the prompt, and `exit` back to
EmuDesk. Repeated entry and exit also restore the desktop correctly.

Boot self-tests run the real shell three times and check file commands,
failed redirection, native tools, history, Tab completion, memory, handles
and DTA restoration. A separate native runtime test redirects stdin, stdout
and stderr independently and checks buffered output on exit. These also
cover duplicated character handles and repeated executable loading from CD.
The host suite uses ASan/UBSan for the utility, parser and command-tail tests
and verifies all twelve `.TTP` payloads in the generated disc image.
See [COMMAND-LINE.md](COMMAND-LINE.md) for usage and supported options.

## Real-console checklist

SD card (serial adapter), not yet run on hardware: format a card with an MBR and
one FAT16 partition, put a few files on it, boot with the adapter attached and
confirm the log shows `E: SD FAT16`. Open E: in EmuDesk, copy a file to it, run
`df`, reboot and confirm the file survives and reads back on a PC. Also boot with
no adapter, and with a FAT32 card, and check the log messages in [SD.md](SD.md).

The File → Boot GD-ROM… action has been checked in Flycast with its boot ROM:
Return selects Cancel and leaves the desktop running; Boot restarts through
the BIOS and reloads the inserted EmuTOS CDI. The HAL closes the D: image before
KOS tears down ISO9660. Physical disc swaps, empty drives and GDEMU behavior
still need console testing. C: files and unsaved settings are lost on Boot.

The CDI is a development test image. Copy it to a GDEMU-compatible card using
your usual image manager, or boot it with your usual Dreamcast disc workflow.
Hardware has not yet been verified. C: is temporary and resets on every boot.
The default CDI omits full-disc filler for small Flycast/GDEMU images. Set
`CD_PADDING=1 ./scripts/build-cdi.sh` if you want a padded CD-R image.

1. Attach a Dreamcast keyboard and mouse, boot, and confirm a stable desktop.
2. Move and click the guest pointer; open C: and D:. Check capture separately
   in Flycast with Left Ctrl+Left Alt, if using the emulator.
3. Launch D:\HELLO.PRG; acknowledge its alert and verify return to EmuDesk.
4. Open C:\NATIVE.TXT using Show and confirm the native program's message.
5. Run D:\VDITEST.PRG; confirm its graphics readback says PASS, then Return.
6. Create a folder on C:, copy a document from D: into it, view then delete it.
7. Try a D: write; it should fail as read-only. Reset and confirm C: resets.
8. Test the available video cable and keyboard region. Report exact hardware,
   cable, image SHA256, failing action and any serial log.

## Limits

This is an initial native platform port, not binary-compatible Atari TOS.
68000 applications, Atari hardware register access and self-modifying 68000
code cannot run. Native programs require porting to the documented ABI.

There is one fixed 640×480 mode. The native glyph renderer supports built-in
fonts, scaling, bold/light/italic/outline and right-angle rotations. Exact pixel
parity with every Atari font-effect combination is not claimed. GDOS font
loading is disabled. No audio, printer,
MIDI, general TSRs, nested
Pexec or crash isolation are implemented. BIOS/XBIOS/GEMDOS expose the subset
needed by EmuDesk and the sample application; unsupported calls must not be
assumed to work. EmuCON is enabled; see [command-line support and limits](COMMAND-LINE.md).

Native desk accessories are supported. Control Panel settings can be saved to
a VMU and restored at boot; this does not persist C: files. Test VMU writes
with isolated cards as described in ACCESSORIES.md. Real-hardware validation
of settings persistence remains pending.

The patched development Flycast build installed locally is used on macOS 27;
the stock installed build was unable to start reliably in this environment.
The image contains no Flycast-specific runtime dependencies.
