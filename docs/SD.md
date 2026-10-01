# SD card on the serial port

EmuTOS can mount FAT16 volumes from an SD card attached to the Dreamcast's
serial port through the common jj1odm/DreamShell-style SCIF adapter. The card
appears as ordinary writable GEMDOS drives, so EmuDesk, EmuCON and native
programs use it like any other drive.

## Card preparation

TOS understands FAT12 and FAT16 only, with 512-byte sectors and at most 65,524
clusters per volume. FAT16 is therefore the format to use; **FAT32 is not
supported** (upstream EmuTOS cannot read it either), and the boot log says so if
it sees one.

- Use an **MBR** partition table (not GPT) with a FAT16 partition of 2 GiB or
  less, or format the whole card as one bare FAT16 volume.
- Partition types 0x01, 0x04, 0x06 and 0x0E are accepted. Up to four primary
  partitions are mounted, in table order, as **E:** to **H:**.
- Larger cards work: make a 2 GiB partition and leave the rest unused. SDHC and
  SDXC cards are read in SPI mode by KallistiOS; capacity beyond the FAT16
  partition is simply not visible to GEMDOS.
- Use cluster sizes up to 32 KiB. Volumes whose reserved area plus FATs and root
  directory exceed 65,535 sectors are refused because a GEMDOS BPB stores those
  values in 16 bits. Ordinary formatters (SD Association formatter, `mkfs.fat -F
  16`, macOS `newfs_msdos`) stay well inside this.
- Long file names are not created or shown; TOS uses 8.3 names. Files that
  Windows saved with long names remain readable through their short names.

## Behaviour and limits

- Applications: the Kilo editor and the image viewer save to any writable drive
  (C: or E:-H:). Puzzle saves and Worm high scores go to the first mounted SD
  drive, falling back to C:; puzzle loads also look on C:. The MP3 player ([audio](AUDIO.md)) browses and plays `.MP3`/`.WAV` files from any mounted drive, defaulting to the SD card. The System Monitor
  lists every mounted drive. Messages say "lost at reset" only for the RAM disk.
  The shared logic is `apps/lib/drives.h`.
- The card is probed **once at boot**. The adapter has no card-detect line, so
  insert or swap cards before powering on or resetting. File → Boot GD-ROM
  restarts and re-probes.
- Writes go straight through GEMDOS's sector buffers. Close files, and let a
  copy finish, before removing power or the card; there is no unmount call.
  GEMDOS flushes dirty sectors when a file is closed.
- The SD probe temporarily suppresses debug output before taking over the
  serial pins for SPI. If card initialization or the capacity read fails, it
  restores the previous console. After successful card initialization, debug
  output stays disabled while SPI owns the port, including if the card has an
  unsupported filesystem. Mount messages therefore are not sent over serial.
- A missing adapter is not an error: the probe fails and C:/D: work as before
  (no delay was observable in Flycast, which has no adapter).
- Transfers are bit-banged SPI over the serial pins, so expect speeds well below
  a memory card slot. Read and write failures are reported to programs as
  GEMDOS read/write faults (`EREADF`, `EWRITF`).
- Hot-plugging, GPT, exFAT, FAT32 and SCI-port adapters are not supported.
  D:, the CD volume, remains read-only. C: remains a RAM disk.

## Implementation

| Piece | File |
|---|---|
| MBR / FAT12-16 boot sector validation, BPB geometry | `src/dreamcast/sd_fat.c` (no KOS code, host-tested) |
| Raw 512-byte block access through KOS `sd_*` | `src/dreamcast/hal_sd.c` |
| Drive letters, BPB, `Rwabs` routing, write policy | `src/dreamcast/storage.c` |

GEMDOS's own FAT code does all file work; the port only supplies block reads and
writes plus the BPB. `tests/test_sd.py` builds synthetic cards (FAT12, FAT16,
bare volumes, four partitions, FAT32, corrupt and truncated volumes, read
failures) and cross-checks the geometry against the host's own formatter when
`newfs_msdos` is present.

Status: verified in Flycast that boot is unaffected when no adapter is present.
**Real adapter and card transfers have not been tested on hardware.**
