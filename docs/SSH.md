# SSH remote terminal

`D:\UTILS\SSH.TTP` connects to SSH-2 servers through the OS's shared KallistiOS
IPv4 stack. Launch it from EmuCON (Ctrl+Z). A network adapter, DHCP lease and
reachable SSH server are required; internet hosts work through the network's
normal gateway. No forwarding, SCP, SFTP or remote-command mode is included.

## One-time preparation

The Dreamcast has no cryptographic hardware random source. The SDK's
clock/RAM random generator is unsuitable for SSH keys. Provision a private
random seed from a computer before connecting:

```sh
python3 tools/prepare_ssh.py /Volumes/SD/SSH
```

Use your mounted card's actual path. This creates `SSH/SEED.BIN`, using the
computer's secure random source. The helper refuses to overwrite an existing
seed. Keep the file private; do not restore an old backup or use copies on
multiple consoles. Each connection derives and writes the next seed, closes
and rereads it **before** producing SSH session randomness. A missing, damaged
or unwritable seed prevents login. If a seed is lost or corrupted, delete it
and generate a fresh one on the computer. Power loss during its update can
require this recovery.

Insert the card before launching SSH. The first writable persistent drive
(normally **E:**) supplies the defaults `E:\SSH\SEED.BIN` and
`E:\SSH\HOSTS.TXT`. The folder must exist. Other drives and paths work with
`-s` and `-K`. Without an SD card the default is C:, but it disappears at reset:
provision a **fresh** seed for each boot and preserve trusted fingerprints
separately. Never put a reusable seed or private identity on a public disc image.

For key login, copy an **OpenSSH-format** private key to the card, using an 8.3
filename such as `E:\SSH\ID_ED255`. The corresponding public key must already
be authorized on the server. Ed25519, RSA up to 4096 bits and ECDSA P-256/P-384/
P-521 identities are supported. Keys can be unencrypted or encrypted with the
OpenSSH default bcrypt/AES-256-CTR format; encrypted keys prompt for a
passphrase. Other encryption formats, hardware-token keys and SSH certificates
are not supported. Unlocking a strongly protected key can take time on SH-4.
Login/password/passphrase prompts currently accept printable ASCII.

## Connecting

```text
ssh alice@example.com
ssh -p 2222 alice@example.com
ssh -i E:\SSH\ID_ED255 alice@example.com
ssh -l alice -s F:\SSH\SEED.BIN -K F:\SSH\HOSTS.TXT example.com
ssh -a interactive alice@example.com
```

Omit the username to be prompted. `-a key`, `-a password` or `-a interactive`
restricts authentication to that method. By default the client tries a supplied
key, then server-offered keyboard-interactive challenges and passwords, with
up to three attempts per password/challenge method. Key plus interactive
multi-factor login works. Passwords, passphrases and challenge responses are
entered locally, never in the command line. EmuCON's command tail is limited
to 126 bytes, so use short paths and defaults when possible.

For a new host, compare its SHA-256 fingerprint with one obtained independently
from the server administrator, then type `yes`. The client saves the pin in
`HOSTS.TXT` before sending credentials. The same host and port must present the
same key on subsequent connections. A changed key is refused; verify a genuine
server key rotation before editing that entry. `HOSTS.TXT` is a simple local
`hostname port SHA256:fingerprint` file, not OpenSSH's `known_hosts` format.
Keep both it and the seed on storage you trust.

## Terminal controls

The client requests `TERM=xterm`, **80 columns × 30 rows**. libvterm handles
cursor movement, scrolling regions, erasure, alternate screens, application
cursor keys, terminal queries and UTF-8 decoding. The native display maps
colours to its 16-colour palette. The Atari font supplies Western glyphs;
unsupported Unicode uses readable approximations or `?`. Wide characters
retain their terminal cell width. Italic and underline styling are not drawn.
Remote window titles and clipboard commands are ignored.

Arrows, Home/End, Insert/Delete, Page Up/Down and F1–F12 are sent to the server.
Alt-letter shortcuts work; Alt+arrows retain the OS keyboard-mouse behavior.
Ctrl+C interrupts the remote process, and Ctrl+D sends normal terminal EOF.
Use a local escape prefix for client actions:

| Keys | Action |
| --- | --- |
| Ctrl+] then `.` | Disconnect, including an unresponsive connection |
| Ctrl+] then `p` / `n` | Previous / next page in the 256-line scrollback |
| Ctrl+] then `r` | Request new SSH session keys |
| Ctrl+] twice | Send literal Ctrl+] |

Typing returns from scrollback to live output. Scrollback is disabled while
an application uses the alternate screen. Logout restores the local console;
the process returns the remote shell's exit status. Connection setup has a
15-second TCP timeout and a 60-second SSH inactivity timeout, excluding time
spent answering prompts. Established sessions have no idle timeout. Transport
rekeying occurs at 256 MiB or one hour, and also accepts server-initiated rekeys.

## Implementation and verification

wolfSSH 1.5.0 handles SSH-2; wolfCrypt 5.9.4 handles cryptography. libvterm 0.3.3
implements the terminal parser. The OpenBSD bcrypt PBKDF from libssh2 1.11.1
unlocks encrypted OpenSSH files. Versions and commits are pinned in
`apps/vendor/sources.json`; build configuration and local patches are described
in `apps/ssh/README.md`. This application is GPL-3.0-or-later, with dependency
notices bundled on D:.

Algorithms include Curve25519/ECDH key exchange, Ed25519/ECDSA/RSA-SHA2 host
signatures, AES-GCM/AES-CTR encryption and HMAC-SHA2. SHA-1 signatures, old
finite-field DH, CBC encryption and SSH-1 are excluded. No server configuration
changes to enable those old algorithms should be needed.

Run `bash scripts/test-ssh.sh` after installing `tests/ssh-requirements.txt`
into `build/ssh-venv`. The tests run the same client sources and TCP adapter
against a local Paramiko server, plus terminal, trust-store and seed checks
under ASan/UBSan. OpenSSH interoperability is also checked locally. The actual SH-4 client was
verified in Flycast with an OpenSSH shell, Vim editing and cursor navigation,
full-screen redraw, Ctrl+C, manual rekey and local disconnect back to EmuCON.
Physical Dreamcast adapter and SD-card testing remains necessary.
