# Native EmuTOS for Sega Dreamcast

[Download the latest CDI](https://github.com/richstokes/dreamcast-EmuTOS-port/releases/download/continuous/emutos-dreamcast.cdi)
or browse [GitHub releases](https://github.com/richstokes/dreamcast-EmuTOS-port/releases/tag/continuous).
Successful CI builds from `main` update these downloads; see [CI details](docs/CI.md).

A native SH-4 port of [EmuTOS](https://emutos.sourceforge.io/), using KallistiOS
for Dreamcast hardware. The actual EmuDesk, AES, VDI and GEMDOS filesystem C
code is compiled for the Dreamcast. **There is no 68000 emulator.** Existing
Atari executables cannot run; applications must be rebuilt for this port's
[native ABI](docs/NATIVE-ABI.md).

The bootable CDI runs in Flycast. Verified operations include launching a
separately compiled SH-4 GEM application and returning to the desktop, reading
files from the disc, creating folders using the keyboard, and RAM-disk file
operations. Real Dreamcast hardware has not yet been tested.

- Display: 640×480, 16-colour planar VDI converted to Dreamcast RGB565.
- Input: Maple keyboard and mouse; controller fallback when no mouse is attached.
- C: 4 MiB FAT16 RAM disk. Contents disappear at reset.
- D: read-only FAT16 volume embedded in the CD's ISO9660 filesystem.
- Serial SD, persistent writable storage and 68000 binary compatibility are absent.

## Build and run

Install a KallistiOS SDK with an SH-4 GCC toolchain. Set `KOS_ENV` to its
`environ.sh`; the default is `~/.local/share/dreamcast/kos/environ.sh`.
The tested SDK is KOS 2.3.0, commit
`cd340378043f00cd1d05284ee0d02548d33d6054`, with SH GCC 15.2.0.
Host tools: Git, C/C++ compiler, make, Python 3, Ninja, pkg-config and libisofs.
On macOS the latter dependencies can be installed with
`brew install ninja pkg-config libisofs`.

```sh
./scripts/bootstrap-mkdcdisc.sh  # one-time pinned disc-image tool build
./scripts/build-cdi.sh
./scripts/run-flycast.sh "$PWD/dist/emutos-dreamcast.cdi"
```

Output: `dist/emutos-dreamcast.cdi`, `dist/emutos-dreamcast.elf` and SHA256SUMS.
`MKDCDISC` can select an existing mkdcdisc executable. `FLYCAST_BIN` can select
Flycast. `./scripts/build.sh` builds only the ELF; direct ELF boot has no D:.
Add files with DOS 8.3 names to `disc/` and rebuild to include them on D:.

For keyboard-only Flycast testing, detach the host mouse route:

```sh
FLYCAST_HOST_MOUSE_PORT=-1 ./scripts/run-flycast.sh "$PWD/dist/emutos-dreamcast.cdi"
```

Alt+C / Alt+D opens a drive. Alt+arrow moves the GEM pointer (Shift adds fine
movement); Alt+Space or Alt+Insert clicks. Select a file, then Ctrl+O opens it.
Return accepts a dialog; Escape leaves the text viewer. Ctrl+N creates a folder.
Flycast's Left Ctrl+Left Alt shortcut toggles mouse capture when those keys are
not mapped to controller actions. The host pointer and the guest pointer can
occupy different positions; use the guest pointer. Automated capture testing
on macOS has not been reliable.

See [hardware testing and limitations](docs/TESTING.md),
[architecture](docs/ARCHITECTURE.md) and [upstream provenance](docs/UPSTREAM.md).
EmuTOS and this port are GPL-2.0-or-later; see COPYING.
