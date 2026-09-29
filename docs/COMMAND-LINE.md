# EmuCON and native command-line tools

Choose **File → Execute EmuCON** or press **Ctrl+Z** on the desktop. Type
`exit` to return to EmuDesk. EmuCON uses the fixed 640×480 display (80 columns,
30 rows), with Up/Down history, cursor-key editing and Tab filename completion.
It starts in the top desktop window's directory, or the boot drive's root.
The default search path is `D:\;C:\`, so bundled commands work from either drive
without specifying an extension. `path` displays or changes that search path.

The shell is the upstream EmuCON2 C implementation, compiled into the OS for
SH-4. It runs without a disc; the extra `.TTP` utilities are on D: in the CDI.
All applications are native Dreamcast programs. Atari 68000 binaries still
cannot run.

## Built-in commands

`help` lists the built-ins; `help copy` explains an individual command.
Aliases include `dir`/`ls`, `type`/`cat`, `copy`/`cp`, `move`/`mv`,
`del`/`rm`, `md`/`mkdir`, `rd`/`rmdir` and `cls`/`clear`.
There are also `cd`, `pwd`, `ren`, `chmod`, `echo`, `more`, `path`, `show`,
`version`, `wrap`, `mode` and `exit`. Enter `C:` or `D:` to switch drives.

C: is writable RAM and is lost at reset. D: is read-only. Use DOS backslashes
and 8.3 filenames. Built-in file commands support wildcards. `more` pages
through text; Space advances a page, Return a line, and Q quits. Ctrl+C
interrupts built-in output or cancels the current command line.

## Extra commands

The following `.TTP` programs are bundled. Each accepts `--help`; text tools
require filenames, with `-` meaning keyboard input. For tools that read to EOF,
press Ctrl+D on an empty input line to finish (Ctrl+Z also works). Ctrl+C
interrupts keyboard input or console output. Double-clicking a `.TTP` on the
desktop opens the usual arguments dialog; using EmuCON keeps the output visible.

| Command | Purpose and options |
| --- | --- |
| `grep [-i] [-n] [-v] TEXT FILE...` | Find literal text; ignore case, number lines, or invert matches. No regular expressions. |
| `wc [-lwc] FILE...` | Count newline characters, words and bytes; show totals for multiple files. |
| `head [-n N] FILE...` | First N lines (default 10). |
| `tail [-n N] FILE...` | Last N lines (default 10). |
| `sort [-r] [-u] FILE...` | Merge and sort text lines by byte order; reverse or remove duplicates. |
| `hexdump FILE...` | Byte offsets, hexadecimal bytes and printable ASCII. |
| `cksum FILE...` | POSIX CRC checksum and byte count. |
| `date` | Current local date/time from the Dreamcast RTC. |
| `df [C: or D:]` | Drive capacity, free space and storage type. |
| `free` | GEMDOS memory, largest free block and KOS heap usage. |
| `uname [-a]` | OS and CPU information; `-a` adds SDK version, uptime and display size. |
| `expr EXPRESSION` | TinyExpr arithmetic, including functions such as `sqrt`, `sin`, `log` and `pow`. |

Network tools (need a Broadband Adapter; see [networking](NETWORKING.md)):

| Command | Purpose and options |
| --- | --- |
| `ifconfig` | Adapter state, MAC, IPv4 address, netmask, gateway, DNS and packet counters. |
| `ping [-c N] [-s BYTES] [-w MS] HOST` | ICMP echo to an address or name; default 4 probes of 56 bytes, 2 s timeout. Ctrl+C prints the summary. |
| `nslookup HOST` | IPv4 lookup through the DHCP-provided DNS server. |

Examples:

```text
help
ls D:\
copy D:\README.TXT C:\NOTES.TXT
grep -in native C:\NOTES.TXT
wc C:\NOTES.TXT
head -n 5 C:\NOTES.TXT
cksum C:\NOTES.TXT
expr sqrt(144)+2^3
free
df
editor C:\NOTES.TXT
exit
```

The existing graphical applications, including `editor`, `calc` and `images`,
can be launched by name. Closing them restores the console. Their usual
memory and filesystem restrictions apply.

## Redirection and limits

`>` replaces an output file, for both built-ins and native utilities:

```text
grep -i dreamcast D:\README.TXT > C:\MATCHES.TXT
wc C:\MATCHES.TXT > C:\COUNT.TXT
type C:\COUNT.TXT
```

Errors from native tools stay on the console. A failed output-file creation
prevents the command from running. The inherited upstream shell does not
provide pipes, input redirection, append redirection, shell scripts, variable
expansion or Unix job control. Extra utilities accept explicit filenames;
EmuCON does not expand wildcards for external programs. `--` ends option
parsing for file utilities. Quote arguments containing spaces.

Text filters normalize CRLF input to LF output, preserve unterminated final
lines, and reject embedded NUL bytes and lines longer than 64 KiB. `wc`,
`hexdump` and `cksum` operate on original bytes. `head` and `tail` accept
0–10,000 lines. `sort` accepts at most 10,000 lines and 1 MiB of text; `tail`
retains at most 1 MiB. Resource-limit and I/O failures return errors, rather
than silently truncating a line. These are compact utilities, not complete
GNU coreutils replacements.

`mode con` reports keyboard delay/rate in 20 ms units. Native keyboard limits
are 5–50 delay units and 1–10 repeat units; values outside those ranges clamp
to the closest limit. Display resolution changes are unsupported.

## Implementation and validation

`src/dreamcast/cli.c` replaces the Motorola startup, BIOS/XBIOS trap stubs and
Line-A geometry reads. The wrapper supplies an environment and private DTA,
and frees history on every exit. Native GEMDOS implements Fdup/Fforce and
console-aware Fread/Fwrite; the newlib runtime writes through inherited
handles, including separately redirected stdout and stderr. GUI launches use
AES's graphics/console transitions and clean up window state afterward.

Boot checks execute the real shell three times, covering built-in file
operations, PATH lookup, native utilities, redirection failures, history,
Tab completion and restored memory/handle/DTA state. Host sanitizer tests
exercise the actual utility sources with CRLF, binary, empty, long-line,
large-file, sorting, checksum and argument-error fixtures. The test suite also
extracts every `.TTP` from the generated FAT image and compares it with its
relocated native build output.
