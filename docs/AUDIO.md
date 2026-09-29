# Audio and the MP3 player

Sound output is an optional native API extension (`audio_*` in
`include/dreamcast/audio.h`, summarised in [NATIVE-ABI.md](NATIVE-ABI.md)),
used by `MP3.PRG`. **None of this has been run on a real Dreamcast or in
Flycast.** The build, the host tests and the hardware-independent logic are
verified; the AICA path, the timing figures below and the SD file reads are not.

## OS side

| Piece | File |
|---|---|
| Argument checks, open/closed state, ABI entry points | `src/dreamcast/audio.c` (no KOS; host-tested) |
| Lock-free single-producer/consumer PCM ring | `include/dreamcast/audio_ring.h` (host-tested) |
| KOS `snd_stream` feeding, thread, volume, pause | `src/dreamcast/hal_audio.c` |

- The sound driver (`snd_stream_init`, which loads the AICA program) starts on
  the **first `audio_open`**, not at boot, so systems that never play sound are
  untouched. If it fails, `audio_open` returns `DC_AUDIO_ERR_UNAVAILABLE`.
- One AICA PCM16 stream (mono or stereo, 8-48 kHz) with a 32 KiB AICA buffer.
  The application's PCM goes into a 32768-frame ring (0.74 s at 44.1 kHz). A KOS
  thread, one priority level above the GEM thread, calls `snd_stream_poll` every
  5 ms, and the stream callback drains the ring, padding with silence (and
  counting an underrun) when it is empty. Sound therefore continues while the
  application decodes, waits for the SD card or redraws.
- `audio_write` never blocks; it returns how many frames fit. Applications
  decode only when `free_frames` is large enough.
- Pause mutes the AICA channel at once and stops draining the ring. Flush
  (used for seeking) empties the ring under a mutex that also guards the
  consumer, and zeroes the position counter.
- `native.c` calls `dc_audio_close()` after every program exit (return or Pterm), so a program cannot leave a stream running. Accessories do
  not use audio.
- `position_frames` counts frames handed to the AICA. Add roughly 0.1-0.2 s of
  AICA buffer latency to what is actually heard; the player compensates for that
  when it draws the spectrum.

## MP3.PRG

Decoding is [minimp3](https://github.com/lieff/minimp3) (CC0, single header,
`apps/vendor/minimp3`), MP3 only (`MINIMP3_ONLY_MP3`), in its floating-point
path, which is native single precision on the SH-4 (`-m4-single`). WAV (8 or 16
bit PCM, mono or stereo) is played too.

- **Browsing:** drive buttons for every mounted drive (C:-H: and beyond); the
  default is the first SD drive (via `apps/lib/drives.h`), else D:. Folders are
  entered with Return, double-click or `..`. Names are 8.3, upper case; only
  `.MP3` and `.WAV` files are listed. The files of the current folder form the
  playlist, in name order; Next/Previous skip folders, Repeat wraps.
- **Controls:** mouse (transport buttons, progress bar seeks, volume bar,
  file list, drive buttons) and keyboard (Space, Return, arrows, Home/End,
  PgUp/PgDn, N, P, S, R, D, `+`/`-`, Backspace, Esc). Left/Right seek by ten
  seconds. Previous restarts the track when more than three seconds in.
  Ctrl+arrows move the window and F5 toggles full size, as in the calculator.
- **Tags:** ID3v2.2/2.3/2.4 (Latin-1, UTF-16, UTF-8; non-ASCII becomes `?`
  because the VDI font is ASCII) and ID3v1 for the title, artist and album.
  Only the first 32 KiB of a tag is examined (cover art follows the text).
- **Length and seek:** the Xing/Info/VBRI frame count when present, else a
  constant-bitrate estimate. Seeking jumps to a proportional byte offset and
  resynchronises, so it is approximate for VBR files without a Xing header.
- **Spectrum:** 24 log-spaced bands from a 512-point FFT of the decoded PCM
  chosen to match what is being heard now, with peak hold. It updates at about
  11 Hz and redraws only the visualiser and progress area (bars are painted
  as level plus background, without clearing first, to avoid flicker).
- **Robustness:** junk, empty, truncated and unsupported files give a message
  and stop; there are no unbounded scans or allocations (256 entries per folder,
  16 KiB input window, 256 KiB junk-skip limit).

### Real-time decoding assessment (estimate, not measured on hardware)

MP3 at 44.1 kHz needs one 1152-sample frame every 26 ms. The Dreamcast's
SH-4 runs at 200 MHz and has a single-precision FPU, but only 8 KiB of
instruction and 16 KiB of data cache, and minimp3's tables and 512-tap
synthesis window compete for it. Reasoning from typical floating-point MP3
decoders on 200 MHz-class CPUs, **a 128 kbps stereo frame should take roughly
5 to 12 ms (about 20 to 45% of the CPU)**, with high-bitrate (256-320 kbps)
files at the upper end because Huffman decoding grows with bitrate. Video
drawing is throttled to 11 Hz and never blocks the audio thread, so the
audio is protected by the 0.74 s ring even if a redraw or a slow SD read takes
a few hundred milliseconds. On an emulator the ratio is not representative.

The player keeps 0.45 s decoded ahead (the ring holds up to 0.74 s), spends at
most 25 ms per pass decoding when that reserve is healthy (up to 150 ms if it
drops below 0.15 s), and calls `yield()` after every frame so the pointer and
keyboard stay responsive. It **measures its own decode load** (decode time /
audio time over two seconds) and shows it with the underrun count, so the
figure can be read off real hardware.

If a file is too heavy, there is no cheap fallback in this decoder: minimp3 has
no half-rate or mono-only decode mode, so downmixing to mono or lowering the
output rate would not cut the work (it would only reduce AICA copying). The
practical mitigation is a lower bitrate (128 kbps is much cheaper than 320);
the visualiser is a small fraction of the cost. If measurement shows real
hardware falls short, the next step would be a decoder tuned for the SH-4 (for
example fixed-point with hand-written assembly); this was not attempted.

### SD card reads

Files are read through GEMDOS in 8 KiB chunks. The SD adapter is bit-banged
SPI and slow ([SD.md](SD.md)); a 320 kbps stream needs 40 KB/s, which should be
within reach, but the measured read speed on real hardware is unknown. The ring
absorbs stalls up to about half a second.

## Tests

`tests/test_mp3.py` builds and runs, with ASan/UBSan:

- `tests/audio_host.c`: the ring (wraparound, full/empty, counter overflow) and
  the ABI shim (argument validation, busy/not-open, short writes when full,
  pause, flush, volume range, idempotent close, format change) against a fake
  hardware layer that uses the same ring header.
- `tests/mp3_host.c`: playlist ordering and stepping, ID3v1/v2.2/v2.3/v2.4
  parsing including 4,000 random hostile tags, MPEG/Xing/VBRI/WAV headers,
  the spectrum analyser, and the real minimp3 decoder against a **generated
  MP3 fixture** (real MPEG-1 Layer III frames with one Huffman-coded spectral
  line, about 785 Hz; no encoder needed). The streamed reader is compared
  bit for bit with a straight in-memory decode across buffer refills, ID3
  skipping and ID3v1 trimming. Finally the whole player runs headless:
  keyboard and mouse events, playlist advance across a sample-rate change
  (which reopens the stream), pause, seek, volume, repeat, folders and drives,
  bad files, a WAV file, an OS without audio and an OS that refuses to open it.

## Real-hardware checklist

1. Put a few 128 kbps MP3s (8.3 names) on the SD card, boot, run MP3.PRG, and
   confirm the file list, ID3 title/artist and sound.
2. Watch the decode load and underruns during playback of a 128 kbps and a
   320 kbps file; try seeking, pause, volume and the visualiser.
3. Exit with Esc and with the window close box; confirm sound stops and the
   desktop works. Start it again to confirm the stream reopens.
4. Report the decode load, underruns and any glitches, with the card adapter.
