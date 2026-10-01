# Dreamcast desk accessories

The CDI loads `CONTROL.ACC`, `MONITOR.ACC` and `VMUTOOL.ACC` at boot, alongside
`CLOCK.ACC`. Open them from **Desk → DC Control**, **System Monitor** or
**VMU Toolbox**. They are native SH-4 GEM programs, and the desktop remains
usable behind their windows. Do not launch `.ACC` files by double-clicking.

Close a window or press **Esc** to hide it; choose its Desk entry to reopen
or raise it. Drag the GEM title and size gadgets, or use **Ctrl+arrows** to
move and **Ctrl+Shift+arrows** to resize. Starting or exiting a foreground
program hides accessory windows; the accessories remain resident. Hidden
accessories wait for messages without running their periodic updates.

## Dreamcast Control Panel

<img src="screenshots/control-panel.jpg" width="640" alt="Dreamcast Control Panel with input settings, a teal desktop and live device diagnostics in Flycast">

| Setting | Range | Default |
|---|---|---|
| Physical Maple mouse speed | 25–400% | 100% |
| Keyboard repeat delay | 100–1000 ms | 300 ms |
| Keyboard repeat interval | 20–200 ms | 40 ms |
| Desktop colour | Original, Teal, Slate, Amber | Original |

**Tab** selects a setting, VMU or action. **Left/Right** adjusts a setting;
**D** restores defaults. The on-screen minus, plus and Defaults buttons
also accept mouse clicks. Changes apply immediately; **S / Save** stores them
on the selected VMU and **L / Load** restores that card's saved settings.
Select **Settings VMU** with Tab and use Left/Right, or click its arrow buttons,
to choose a connected card. Saving writes only `EMUTOS.CFG`, a two-block
(1024-byte) EmuTOS save with a 32×32 colour settings icon. Keep the card inserted
until the result appears. Original one-block saves still load and gain the icon
on the next Save, requiring one extra free block.
Defaults changes the session; press Save to make those defaults persistent.

At boot, `CONTROL.ACC` reads connected cards in port/unit order and applies
the first valid save, including the desktop colour. Missing or invalid saves
leave the initial settings unchanged. Reopening the panel does not reload or
discard unsaved changes. Cards inserted later can be selected and loaded manually.
There are no automatic writes; once the icon is present, repeated saves of
identical data do not rewrite the card. Failed loads leave the current settings
unchanged. Full, missing,
unformatted or damaged cards display an error. Foreign, protected or malformed
VMS files named `EMUTOS.CFG` are not overwritten. Save verifies the data before
reporting success. VMU Toolbox remains read-only.

Mouse speed does not change the Alt+arrow keyboard pointer or controller
fallback speed; fractional movement is retained at slow mouse settings.

The live input section displays the most recent raw mouse motion, mouse
buttons, GEM pointer position, keyboard key/modifier codes and event counts,
and controller buttons, stick and triggers. It samples cached input every
200 ms without consuming extra input events. This helps distinguish the
Dreamcast pointer from Flycast's separate host cursor.

Desktop colour changes palette entry 3, so other objects using that colour
also change. Bundled fullscreen programs save and restore the palette.
Resolution and controller remapping are not configurable here.

## System Monitor

<img src="screenshots/system-monitor.jpg" width="640" alt="System Monitor showing RAM, disk space, a memory history graph and Maple devices in Flycast">

Shows console uptime and RAM, GEM free/total memory and its largest free
block (in KiB), KOS heap usage, C:/D: free space, and attached Maple devices.
The graph records the last 60 visible samples of **GEM free memory**, with a
one-second sampling interval. There is no estimated CPU-load percentage.
GEM allocations live within the OS's memory arrangement; GEM and KOS figures
are different allocator views and should not be added as independent pools.

**Space** pauses/resumes sampling, **R** takes a fresh sample, and **Up/Down**
scrolls the device list. Reopening the window refreshes the snapshot.
C: is still temporary RAM storage and D: is still read-only.

## VMU Toolbox (read only)

<img src="screenshots/vmu-toolbox.jpg" width="640" alt="Read-only VMU Toolbox listing synthetic test files, free blocks, copy flags and a selected file timestamp in Flycast">

Lists connected VMUs/memory cards, their free/total user blocks, filenames,
block counts, Data/Game type, copy-protection flag and modification time.
Selecting a file also shows its allocated size, first block and header
block offset. VMU blocks are 512 bytes; allocated size includes any file
headers and padding. The screenshot uses synthetic test metadata.

- **Left/Right:** switch cards (A1 means port A, unit 1).
- **Up/Down:** select a file; **Page Up/Page Down:** move eight files.
- **R:** refresh the selected card. Connect a card, then press R to inspect it.
- Mouse buttons switch cards/refresh; click a row to select it.

The service issues **VMU block reads only**. It reads root, FAT and directory
metadata on opening, card selection and explicit refresh. Its timer only
enumerates connected devices and invalidates stale listings when it observes
a device change. The Toolbox has no format, delete, restore, write, LCD-update or
file-content operations, and stays read-only: use the
[VMU Editor](#vmu-editor-vmuedit-prg) program to change a card. The displayed
copy flag is metadata, not an action.

Supported layouts are standard 128 KiB VMU metadata with one FAT block and
up to 16 directory blocks. Unformatted, unreadable, damaged or unsupported
cards show a status message. Bounds, file chains, cycles and cross-links
are checked before displaying a successful snapshot. Nonstandard expanded
cards may be unsupported. Save descriptions/icons inside VMS payloads are
not decoded. Physical hardware validation remains pending.

## VMU Editor (VMUEDIT.PRG)

`VMUEDIT.PRG` is a fullscreen program on D: (start it from EmuDesk, not the Desk
menu). It has three screens: a file manager, a 48x32 LCD editor and a VMS save-icon
editor. It works on the same cards as the Toolbox and changes them only through
the OS [VMU file service](NATIVE-ABI.md), which has the safety rules listed
there. Mouse, keyboard and controller (pointer and A/B as mouse buttons) work
everywhere; every screen shows its keys.

**Files** (card selector, directory, free blocks): `Left/Right` card, `Up/Down`
file, `R` refresh (cards are never read in the background), then
- `I` **import** a file from C:, D: or an SD drive (E:-H:) with a built-in file
  chooser (Enter opens, Backspace goes up, Tab changes drive). The default name
  is the file name without a `.VMS` extension, editable to 1-12 characters. It
  is stored as an ordinary data file, zero-padded to 512-byte blocks; the limit
  is 200 blocks (100 KiB). An existing name needs a separate "Replace" answer.
- `E` **export** the selected file as its raw block image (`blocks x 512` bytes) to
  a writable drive. The default name keeps a short extension from the VMU name or
  uses `.VMS` (so `SONICADV_SYS` becomes `SONICADV.VMS`); read-only drives (D:)
  are refused and an existing file needs confirmation. A `.VMS` written this way
  is the file exactly as the VMU holds it; the tool does not use the byte-swapped
  Nexus **`.DCI`** container and does not read or write it, because it could not
  be checked against real DCI files here. Copy-protected files are not exported.
- `N` **rename**: copies the file to the new name, verifies it, then deletes the
  old one (it therefore needs free blocks equal to the file's size, refuses an
  existing target name, and gives the file a new timestamp and position).
- `D` / `Delete` **delete** after a confirmation that defaults to Cancel.
- `L` opens the LCD editor and `C` the icon editor for the selected file.

Import, export, rename, delete and icon saves ask before acting, with the
destructive answer never the default. Files that are copy-protected, games
(type 0xcc) or have an unusual header offset are listed but never overwritten,
renamed or deleted (use the Dreamcast BIOS file manager for those). Damaged,
unformatted or unsupported cards are shown with a reason and no write is
attempted.

**LCD editor** (48x32 monochrome): the 1:1 and 2x previews match what the VMU
shows. Left button draws, right button erases (drag draws a gap-free line);
arrows move a cursor, `Space` toggles, `D`/`E` set dark/light, `P` cycles a pen
(arrows then paint), `I` invert, `C` clear, `H`/`V` flip, `[ ] - =` shift with
wrap, `Z` undo/redo. **Live** (`T`, default on) sends the bitmap to the selected
card's LCD after each edit (at most about every 80 ms; a busy frame is retried);
`U` sends it once. The LCD image is not stored on the card and the BIOS
restores its own display when the VMU is used. `L` loads and `S` saves a 1-bit
`.BMP` (or a raw 192-byte `.LCD`: six bytes per row, top row first, most
significant bit leftmost, 1 = dark). Loading accepts uncompressed 1, 4, 8, 24
and 32-bit BMPs of any size up to 4096x4096, thresholds to black/white and
crops or pads to 48x32. Quitting with an unsaved bitmap asks first.

**Icon editor** (32x32, 4 bits per pixel, 16 ARGB4444 palette colours): available
for ordinary data files whose header is a valid VMS save (header length
and CRC-16 are checked; anything else is refused with the reason). Up to three
animation frames are supported. Left button paints, right button picks a colour,
`Space` paints at the cursor, `X` picks, `, .` choose a colour, `R G B A` raise
and `r g b a` lower that colour's channel, `[ ]` change frame, `H V F` flip or
fill the frame, `Z` undo. Only the palette and icon pixels change; text fields,
data, eyecatch and padding are kept byte for byte and the CRC is recomputed,
then the whole file is rewritten and verified (`S`, after a confirmation).
Because the display has 16 colours, the screen chrome uses black and white
and the icon's palette is mapped onto the other 14 hardware colours: if an icon
uses more than 14 distinct visible colours the rarest are shown as their nearest
match (a note says so); the file itself is unaffected. Fully transparent entries
are drawn white with a dot. The eyecatch image, animation speed and text fields
are not editable.

Rewriting a file is done by KOS as delete-then-allocate, so an overwritten file may
move to different blocks and gets a new timestamp. The OS keeps the old contents
as a rollback copy and restores them if the write fails on a still-consistent
card; a power cut or card removal in the middle of a write can still leave a card
that needs checking in the console's own file manager. Keep the card connected
while the message "keep the card connected" is shown. This was verified only
on the host (see below); it has not been run on real VMUs or in Flycast.

To try the editor in Flycast without touching real saves, make an isolated card
directory (the generator refuses to overwrite) and launch with it; unlike the
Toolbox test, A1 here is expected to change:

```sh
python3 tools/vmu_fixture.py --editor build/vmu-edit-test
FLYCAST_VMU_DIR="$PWD/build/vmu-edit-test" FLYCAST_HOST_MOUSE_PORT=-1 \
  ./scripts/run-flycast.sh "$PWD/dist/DreamTOS.cdi"
```

A1 then holds two VMS saves with icons (`ICONTEST`, three-frame `ANIMATED.001`), a
raw file, a copy-protected file and a game, for import/export/rename/delete and
icon editing; A2 is empty. This Flycast run has not been done yet.

Host tests (`tests/test_vmu_write.py`, `tests/test_vmuedit.py`) cover the file
service against KOS's own unmodified `vmufs.c` and a step-for-step double: creation,
multi-block allocation order, overwrite (grow, shrink, exact), 200-block and
directory-full limits, deletion, name handling, cross-links/cycles/short and
long chains/bad layouts refused with zero writes, copy-protected/game/odd
files untouched, unreadable metadata, injected write failures and silent bit
flips (with rollback or an honest verification failure), and the editor's
import/export/rename/delete, LCD and icon behaviour through synthesized input.
`tools/vmu_fixture.py` provides the writable card model (`VmuCard`) and a
consistency checker used as the independent oracle.

## Repeatable Flycast testing

Use isolated synthetic card images to exercise scrolling and empty-card
states without using existing saved cards. Choose a new output directory;
the generator refuses to overwrite existing VMU images. These are metadata
test fixtures, not playable game saves.

```sh
python3 tools/vmu_fixture.py build/vmu-test
FLYCAST_VMU_DIR="$PWD/build/vmu-test" FLYCAST_HOST_MOUSE_PORT=-1 \
  ./scripts/run-flycast.sh "$PWD/dist/DreamTOS.cdi"
```

The launch override is transient. A1 contains 12 entries using 24 blocks
(176 of 200 free); A2 is empty. After a toolbox-only test, exit Flycast and verify both images:

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib, json
p = Path('build/vmu-test')
for name, expected in json.loads((p / 'SHA256.json').read_text()).items():
    assert hashlib.sha256((p / name).read_bytes()).hexdigest() == expected, name
print('VMU images unchanged')
PY
```

Flycast validation covered settings/defaults/desktop colour, monitor
pause/resume and device scrolling, populated and empty cards, file scrolling
and timestamps, three accessory windows together, and reopening all three
after a calculator launch/exit. Both synthetic VMU images and all 267
pre-existing Flycast VMU images retained their SHA-256 checksums after testing.

Host ASan/UBSan tests exercise settings validation, fractional mouse scaling,
bounded metadata reads, malformed chains/cycles/cross-links, failed reads,
read-only buffer integrity, frontends, hidden timers, cancelled/obscured
clicks, window lifecycle and compatibility with older API tables. FAT-image
checks verify all four relocated `.ACC` files are included on D:.

For settings persistence, create a fresh fixture directory and launch with
that `FLYCAST_VMU_DIR`. In DC Control, change all four settings, select **A2**
and press **S**. Press **D**, then **L**, to verify restoration. Quit Flycast
and launch again with the same directory: the preferences should restore
before opening the panel. A1 must retain its original checksum; A2 is expected
to change and contain a two-block `EMUTOS.CFG` with one icon frame.

This save/defaults/load/cold-boot sequence passed in Flycast with mouse speed
175%, repeat delay 600 ms, interval 80 ms and Slate. The saved VMS checksum,
payload and zero padding were checked independently, and A1 was unchanged.
Updating the existing save and loading the replacement also passed.
Original iconless saves were loaded and upgraded without changing their settings;
the icon palette, pixels and VMS checksum were checked in the resulting file.
Sanitized host tests cover invalid settings, missing/full/damaged cards,
foreign files, fragmented two-block saves, legacy migration with insufficient
space, failed writes/readback, unchanged-save suppression, card
selection, no periodic file I/O and compatibility with older API tables.
Physical VMU write testing remains pending, for the settings save and for the VMU Editor.
