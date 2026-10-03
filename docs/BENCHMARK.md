# Native Dreamcast benchmark

`BENCH.PRG` is an original GPL-2.0-or-later SH-4 application. It is included
in `D:\UTILS` on the normal CDI alongside a generated 256 KiB `BENCH.DAT`
read-test file.
It contains no zBench code and does not reproduce zBench's Atari scores.

Open `D:\UTILS\BENCH.PRG` from EmuDesk. **R** or **Return** runs all twelve tests.
**Escape** during a run cancels it, retaining completed results; Escape when
idle exits. **S** saves the results and methodology to `C:\BENCH.TXT`, replacing
an earlier report. **E** cycles the report's environment label through
Unspecified, Real hardware and Flycast. This is a user-supplied label: the
program does not claim it can detect an emulator. Select it before saving.
The report and all other C: contents disappear at reset or power-off.

## What is measured

| Test | Work unit / reported rate |
| --- | --- |
| Integer | 1,024 dependent xorshift32 iterations; million iterations/s |
| Float / double | 1,024 dependent multiply-plus-add updates; million updates/s |
| Memory copy | `memcpy` between two 256 KiB buffers; payload MiB/s |
| Memory read | Sum volatile 32-bit words in a 256 KiB buffer; MiB/s |
| Memory write | Store volatile 32-bit words in a 256 KiB buffer; MiB/s |
| VDI lines | One line spanning 608 pixels; lines/s |
| VDI fills | One 80x32 rectangle; fills/s |
| VDI text | One transparent 24-character string; strings/s |
| C: write / read | Repeated 256 KiB RAM-file transfers in 16 KiB requests; MiB/s |
| D: read | Repeated reads of the bundled 256 KiB file; MiB/s |

Each test warms up and doubles its batch until it takes at least 16 ms.
Three samples each accumulate at least 250 ms of measured work, using the
native monotonic millisecond timer. The display shows the median rate and
minimum/maximum rates. The report also preserves each sample's work-unit
count and duration in acquisition order; multiply units by the amounts in
the table to reconstruct rates. No CPU clock or MIPS/FLOPS estimate is made.
The application normally finishes in tens of seconds; slow environments can
take longer. Cancellation is checked between batches.

UI polling and framebuffer presentation between batches are excluded from
timed work. Interrupts and KOS scheduling remain enabled. Graphics timings
include the toolkit's VDI attribute calls, and measure drawing into EmuTOS's
planar screen, excluding the later conversion/presentation to RGB565. They
are not frame-rate or PowerVR benchmarks. The test area is 608x112 pixels
within the fixed 640x480, 16-colour display.

The arithmetic kernels retain their outputs and use no fast-math. FP contraction is disabled for reproducibility. FP tests
measure dependent arithmetic throughput, not peak independent instruction
throughput. Memory uses normal cached allocations; copy counts bytes copied
once rather than adding read and write traffic. Read includes summation.
The application validates copied/stored memory and file data outside timing;
failed tests do not display successful rates.

C: file tests run only when the OS identifies C: as writable and RAM-backed.
They exclusively create an unused `C:\BNCH0000.TMP` through `BNCH0099.TMP`,
preallocate/fill it, and remove only that owned file after completion or
cancellation. Existing files with those names are preserved. File rates
include seeks and transfers, but exclude creation, verification and cleanup.
Missing files, insufficient space and I/O failures are reported individually.

D: reads include filesystem and available caching effects. They do **not**
measure raw GD-ROM speed. Flycast results describe that emulator run and must
not be represented as physical Dreamcast performance. Compare the same
benchmark version, compiler/build flags and environment settings.

## Build and validation

`./scripts/build-cdi.sh` builds and packages the benchmark. The native runtime
also supplies newlib's reentrant unlink wrapper for temporary-file cleanup;
the boot-time runtime diagnostic checks deletion and subsequent ENOENT.

`python3 -m unittest discover -s tests -p test_bench.py -v` checks timing
accounting, timer wrap, cancellation, stalled timers, work failures, memory
kernels, real file round trips, existing-file preservation, truncated reads,
read-only writes, report saving and bundle staging under host sanitizers.

Flycast verification (2026-09-28): all twelve tests completed with valid
results; the full report saved and reopened in EmuDesk with all raw samples;
cancelling a second run retained completed rows; exiting restored the desktop
and removed the temporary test file. Boot-time runtime checks, including
file deletion, passed. Physical Dreamcast validation remains pending.
