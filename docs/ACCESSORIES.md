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

**Tab** selects a setting or Defaults. **Left/Right** adjusts a setting;
**D** restores defaults. The on-screen minus, plus and Defaults buttons
also accept mouse clicks. Changes apply immediately and last until reset.
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
a device change. There are no format, delete, restore, write, LCD-update or
file-content operations. The displayed copy flag is metadata, not an action.

Supported layouts are standard 128 KiB VMU metadata with one FAT block and
up to 16 directory blocks. Unformatted, unreadable, damaged or unsupported
cards show a status message. Bounds, file chains, cycles and cross-links
are checked before displaying a successful snapshot. Nonstandard expanded
cards may be unsupported. Save descriptions/icons inside VMS payloads are
not decoded. Physical hardware validation remains pending.

## Repeatable Flycast testing

Use isolated synthetic card images to exercise scrolling and empty-card
states without using existing saved cards. Choose a new output directory;
the generator refuses to overwrite existing VMU images. These are metadata
test fixtures, not playable game saves.

```sh
python3 tools/vmu_fixture.py build/vmu-test
FLYCAST_VMU_DIR="$PWD/build/vmu-test" FLYCAST_HOST_MOUSE_PORT=-1 \
  ./scripts/run-flycast.sh "$PWD/dist/emutos-dreamcast.cdi"
```

The launch override is transient. A1 contains 12 entries using 24 blocks
(176 of 200 free); A2 is empty. After exiting Flycast, verify both images:

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
