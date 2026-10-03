# Testing and current limits

## Verified in Flycast

Calculator is now accessory-only. Earlier checks below involving the `calc`
command used the former standalone program; open the current calculator from
**Desk → Calculator**.

2026-10-03, calculator accessory: the rebuilt CDI loads all five accessories.
**Desk → Calculator** evaluates `7+8` as `15` while the desktop remains active;
File → Open opens a D: folder beside it, and the Desk entry raises the existing
calculator. Esc hides it. After entering EmuCON, launching/exiting `calc`, and
returning to EmuDesk, reopening the accessory retains `7+8 = 15`; `ans+2` then
evaluates to `17`. Ctrl+Shift+arrows resize it and Ctrl+arrows move it beside
the folder. This used an isolated Flycast profile with no VMU attached.
Host tests pass, including accessory lifecycle/input/state,
resident heap ownership across foreground cleanup, unload reclamation, and
the relocated `CALC.ACC` payload in the disc image. Real-hardware accessory
validation remains pending.

2026-10-03, grouped application bundle: EmuDesk displays `APPS`, `GAMES` and
`UTILS` on D:. Boot-time loader, runtime and EmuCON checks pass with the new
paths, and all four root-level accessories still load. Bare-name `calc`,
`mines`, `ftp` and `bench` launches succeed through PATH; FTP browses into
`D:\APPS`. All twelve benchmark rows complete, including reads from
`D:\UTILS\BENCH.DAT`. The host suite passes 113 tests, including nested and
multi-cluster directories, parent entries, capacity checks and bundled payloads.

The dated checks below used the former root-level program paths; current
diagnostics live in `D:\UTILS`.

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

### Diagnosing a black screen

Boot-time alignment faults were reproduced on 2026-09-30 using an instrumented
Flycast interpreter that checks SH-4 memory-access alignment. The packed OS
could place its native API table at a two-byte boundary, while the application's
runtime reads it with four-byte loads. The bundled runtime startup check then
faulted before the desktop. Further checks caught unaligned bitmap-pointer and
AES event-list reads during GEM startup. The table now requires four-byte
alignment, and the affected GEM pointer accesses use packing-aware operations.
Related window-pointer and graphics accesses were corrected as well. Ordinary
Flycast runs had not exposed these faults.

With alignment checks enabled, the corrected CDI reaches EmuDesk and opens D:
in both 640x480 interlaced TV mode and 640x480 VGA mode. The VGA run also enters
EmuCON and launches the graphical calculator without an alignment exception.
All startup self-tests and the 83-test host suite pass.

On 2026-09-30, a real Dreamcast with GDEMU, an HDMI adapter detected as VGA,
Broadband Adapter, controller and VMU passed those startup checks and reached a
visible desktop using a dcload-ip ELF upload. Direct boot of the corrected CDI
from GDEMU was subsequently confirmed on real hardware.

The image displays a blue startup screen as soon as the application has set
the video mode. It names the current step: opening the CD, creating the RAM
disk, reading the filesystem, probing serial SD, running startup checks, or
starting GEM. If startup stops, report the full stage text. A fatal EmuTOS
error or unhandled SH-4 exception displays a red screen; report its message
and the exception, PC and PR values. These screens use the built-in font and
direct framebuffer writes, so they do not depend on GEM or a serial cable.

If no EmuTOS screen appears at all, report whether the Dreamcast/SEGA logos
appeared, the boot method (CD-R, GDEMU, etc.), console region and video cable,
and the image SHA256. Failure before the application sets up video still
requires checking the disc bootstrap or KOS initialization. Successful
Flycast boot alone does not verify those paths on a console.

The graphical SD formatter was launched from `D:\UTILS\SDFORMAT.PRG` in Flycast
on 2026-10-03; its no-card state disables formatting and shows C:/D: protection.
The README screenshot is cropped to the GEM window. Sanitized host tests cover
format geometry, metadata read-back failures, open-file rejection, storage
cache detachment, protected targets, confirmation, and remount/reboot outcomes.
Generated volumes pass the host FAT checker. Real SD formatting remains
unverified: use a spare card, check both a blank/FAT32 card (immediate E: mount)
and an already-accessed FAT16 card (reboot prompt), then copy a file, reboot,
and read it back on a PC.

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
Direct CDI boot from GDEMU has been confirmed on a real Dreamcast.
C: is temporary and resets on every boot.
The default CDI omits full-disc filler for small Flycast/GDEMU images. Set
`CD_PADDING=1 ./scripts/build-cdi.sh` if you want a padded CD-R image.

1. Attach a Dreamcast keyboard and mouse, boot, and confirm a stable desktop.
2. Move and click the guest pointer; open C: and D:. Check capture separately
   in Flycast with Left Ctrl+Left Alt, if using the emulator.
3. Launch D:\UTILS\HELLO.PRG; acknowledge its alert and verify return to EmuDesk.
4. Open C:\NATIVE.TXT using Show and confirm the native program's message.
5. Run D:\UTILS\VDITEST.PRG; confirm its graphics readback says PASS, then Return.
6. Create a folder on C:, copy a document from D: into it, view then delete it.
7. Try a D: write; it should fail as read-only. Reset and confirm C: resets.
8. Test the available video cable and keyboard region. Report exact hardware,
   cable, image SHA256, failing action and any serial log.

Pointer response, 2026-10-01: a real console showed the pointer coasting after
the mouse stopped, and a folder window's sizer ignoring quick drags. Each screen
update converted the whole frame, and one Maple packet was consumed per poll, so
packets queued up and the AES did not dispatch while the mouse kept moving. The
HAL now uploads only changed scanlines, each poll drains the queue, and a click
is reported where the button went down. Scripted packets in Flycast (a press on
the sizer followed at once by 20-pixel steps every 16 ms) resize a D: window to
the release point. On hardware, confirm the pointer stops with the mouse and
that a quick drag of the sizer and of the title bar both follow the pointer.

Audio: see the checklist in [AUDIO.md](AUDIO.md#real-hardware-checklist).

## Network-upload hardware testing

With dcload-ip configured to auto-start from openMenu, run:

```sh
./scripts/build.sh
python3 scripts/test-console.py
```

The script uses the local Shelly RPC API to turn relay 0 off for one second
and restore power, then polls the actual dcload-ip protocol for up to 60 seconds.
It uploads the ELF as soon as the loader responds, maps `build/disc` to `/pc/`,
and keeps dc-tool-ip attached to serve files and capture console output. The
default setup is console `192.168.1.171`, plug `192.168.1.173`, and the uploader
at `~/Dropbox/Games/ROMs/DREAMCAST/dcload-ip/dc-tool-ip`. The plug identity is
checked before switching power; addresses, identity, ELF and tool paths can be
overridden with the options shown by `--help`.

Logs are saved to `build/console-*.log`. `Dream TOS: desktop ready` marks entry to
the desktop event loop; exceptions and test failures remain in the same log.
The complete automated cycle was verified on 2026-09-30: dcload replied 53
seconds after power-on, the ELF uploaded, all six startup self-tests passed,
and `Dream TOS: desktop ready` appeared with all four accessories loaded.
Ctrl+C stops the host console/fileserver and leaves power on. Keep it running
while using D: from the console. Stop the previous uploader before the next run.
`--no-power-cycle` uploads to an already running loader; `--power-only` restarts
and waits for the loader without uploading. Each invocation performs one run.

If `build/disc/DISC.IMG` is missing or the app bundle needs rebuilding, run
`./scripts/build-cdi.sh` first. The ELF tries `/cd/DISC.IMG`, then
`/pc/DISC.IMG` when booted through dcload, so the same D: files and startup tests
work without replacing the GDEMU image. Files are opened read-only. Under
dcload-ip the Ethernet adapter stays with the loader, so native network utilities
are unavailable; ordinary disc boots retain native networking. Reinitializing
the adapter during a loader boot previously stalled its console/file syscalls.

The power command uses Shelly's one-shot `toggle_after` restore timer and also
explicitly confirms power on; see the [Switch API](https://shelly-api-docs.shelly.cloud/gen2/ComponentsAndServices/Switch/).

## Limits

This is an initial native platform port, not binary-compatible Atari TOS.
68000 applications, Atari hardware register access and self-modifying 68000
code cannot run. Native programs require porting to the documented ABI.

There is one fixed 640×480 mode. The native glyph renderer supports built-in
fonts, scaling, bold/light/italic/outline and right-angle rotations. Exact pixel
parity with every Atari font-effect combination is not claimed. GDOS font
loading is disabled. Audio exists only through the optional native API used by
the MP3 player ([audio](AUDIO.md), unverified on hardware). No printer,
MIDI, general TSRs, nested
Pexec or crash isolation are implemented. BIOS/XBIOS/GEMDOS expose the subset
needed by EmuDesk and the sample application; unsupported calls must not be
assumed to work. EmuCON is enabled; see [command-line support and limits](COMMAND-LINE.md).

The VMU file service and VMUEDIT.PRG have only host coverage
(`tests/test_vmu_write.py`, `tests/test_vmuedit.py`: in-memory cards, KOS's
`vmufs.c` compiled on the host, injected faults). They have not run on a real VMU or
in Flycast, and real flash timing, card removal mid-write and `vmu_draw_lcd_rotated`
orientation on a physical LCD are unverified; try a spare card first.

Native desk accessories are supported. Control Panel settings can be saved to
a VMU and restored at boot; this does not persist C: files. Test VMU writes
with isolated cards as described in ACCESSORIES.md. Real-hardware validation
of settings persistence remains pending.

The patched development Flycast build installed locally is used on macOS 27;
the stock installed build was unable to start reliably in this environment.
The image contains no Flycast-specific runtime dependencies.

## SSH client

See [SSH setup and verification](SSH.md). `bash scripts/test-ssh.sh` builds the
real client and TCP adapter under ASan/UBSan, runs terminal/seed/trust-store and
native keyboard checks, then fifteen live Paramiko protocol cases (requires
`tests/ssh-requirements.txt` in `build/ssh-venv`). Tests include password,
interactive and encrypted RSA/ECDSA/Ed25519 identities, multi-factor login,
host-key pin reuse/refusal, rekeying and receive-window exhaustion. Optional
seed coverage includes explicit acceptance, cancellation before TCP, missing
fingerprint-folder creation, encrypted-key login and host-key refusal with
weak randomness, and preserving damaged seeds without replacing them. Native
Flycast checks against OpenSSH cover shell input, 80x30 PTY, Vim navigation/
redraw, Ctrl+C, rekey and local disconnect. A native Flycast check against the
local Paramiko fixture also verified declining the risk prompt, accepting it
on a fresh C: drive, automatic fingerprint-folder creation, password login,
logout and saving only HOSTS.TXT (no weak seed file). No test credentials enter
the CDI.
