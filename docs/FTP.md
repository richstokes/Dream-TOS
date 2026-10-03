# Graphical FTP server

Open **D:\UTILS\FTP.PRG** in EmuDesk. The initial window asks which drive or folder
to share. Choose **C: RAM**, **D: Disc**, or a mounted **E:–H: SD** volume.
The first writable SD volume is selected initially; without one, it selects C:.
SD cards must be inserted before boot and use a supported FAT16 layout;
see [SD card support](SD.md).

Select a folder and click **Open**, or use Up/Down then Return. **Up** goes to
its parent; **Prev/Next** page through large folder lists. Click **Start here**
to share the displayed folder, including its subfolders. Sharing a drive root
shares that whole drive. Nothing listens until Start is pressed.

The running window shows the FTP address, connection/transfer status and bytes
transferred. Connect to that address on **port 21**, using plain FTP, **passive
mode** and **binary** file transfers. Choose anonymous login in the client;
there is no password check, and commands also work without logging in.
Use one connection at a time in clients that otherwise open parallel sessions.
The app uses the existing KallistiOS Broadband/LAN Adapter stack and DHCP lease.

**Stop** ends the session and returns to folder selection. **Quit**, Escape or
the window close box stops the server, closes files and releases sockets before
returning to EmuDesk. Keep the application open while sharing: this is a
foreground program, not a resident accessory. Window movement, redraw and Stop
continue to work during transfers. A lost/changed network address stops sharing;
press Start again after the connection recovers.

Keyboard: **C–H** select a mounted drive, **Up/Down** select a folder,
**Return/O** open it, **U/Backspace** go up, **</>** or **PgUp/PgDn** page,
**S** starts/stops and **Esc** quits.

## File behavior

- The selected folder becomes `/`. Clients cannot change drives or navigate
  above it. Paths use `/` and DOS 8.3 names; unsupported names are rejected
  rather than silently shortened. Names are case-insensitive.
- C: and mounted SD volumes support downloads, uploads, folder creation,
  deletion and rename. D: remains read-only. Anyone who can reach the server
  has that access without credentials; transfers use unencrypted FTP.
- Uploads replace existing files. An interrupted or failed upload may leave a
  partial file, including when Stop is pressed. A `226` completion is sent only
  after the file has closed successfully. Files are opened for upload only once
  the data connection arrives. C: contents disappear at reset.
- Supports PASV/EPSV, LIST/NLST/MLSD, CWD/CDUP/PWD, SIZE, RETR/STOR,
  MKD/RMD/DELE, RNFR/RNTO, ABOR and common client setup commands. File transfers
  require TYPE I (or L 8); TYPE A is accepted for directory listings.
- One client, a 4 KiB transfer buffer and bounded work per event-loop tick.
  Data connections time out after 30 seconds without progress; idle control
  sessions time out after five minutes. Passive data connections must come
  from the control connection's IPv4 address.
- Active mode, TLS/SFTP, resume/append, long filenames and timestamp preservation
  are not implemented. LIST uses a placeholder date; MLSD reports type and size.

## Build and verification

`./scripts/build-cdi.sh` includes `FTP.PRG` in `D:\UTILS` and rebuilds the OS with the
appended non-blocking TCP native API. An older OS reports TCP as unavailable.
The existing native ABI prefix is unchanged; see [NATIVE-ABI](NATIVE-ABI.md).

`python3 -m unittest discover -s tests -p 'test_ftp.py' -v` exercises real FTP
sessions with Python's standard FTP client against the actual engine and TCP
boundary, using a host GEMDOS fixture, under ASan/UBSan. It covers anonymous
access, PASV/EPSV, listing, binary round trips, SD/subfolder paths, read-only
shares, traversal and malformed commands, short sends, timeouts, aborts,
reconnects, file failures and socket cleanup. A GEM window model checks the
picker, paging, SD preference, network states, old APIs and clipped drawing.

Real Dreamcast/BBA/SD transfers still need hardware verification. Flycast's
native SH-4 run on 2026-10-03 verified the startup picker, C: subfolder sharing,
port-21 listening with the DHCP address, Stop/restart on read-only D:, and clean
exit to EmuCON. No SD adapter is emulated in that run.

Flycast's standard PicoTCP configuration supplies outbound NAT; it does not automatically
expose this server's port 21 and passive data ports to the host. The stock
launcher alone is therefore insufficient for a host-to-guest FTP transfer test.
Boot from CDI/GDEMU for native networking: dcload-ip owns the adapter during a
network-upload boot.

Protocol references: [FTP (RFC 959)](https://www.rfc-editor.org/rfc/rfc959) and
[EPSV (RFC 2428)](https://www.rfc-editor.org/rfc/rfc2428).
