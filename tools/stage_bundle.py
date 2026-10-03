#!/usr/bin/env python3
"""Stage exactly the supported native apps and their redistribution notices."""
import argparse
import shutil
from pathlib import Path

APP_GROUPS = {
    'APPS': ('EDITOR', 'IMAGES', 'PAINT', 'MP3'),
    'GAMES': ('FIFTEEN', 'MINES', 'NET', 'WORM', 'BLOCKS'),
    'UTILS': ('FTP', 'VMUEDIT', 'SDFORMAT', 'SYSINFO', 'BENCH', 'HELLO', 'VDITEST', 'RUNTIME'),
}
APPS = tuple(app for group in APP_GROUPS.values() for app in group)
CLI_TOOLS = ('GREP', 'WC', 'HEAD', 'TAIL', 'SORT', 'HEXDUMP', 'CKSUM', 'DATE', 'DF', 'FREE', 'UNAME', 'EXPR')
NET_TOOLS = ('PING', 'NSLOOKUP', 'IFCONFIG')
SSH_TOOLS = ('SSH',)
NOTICES = {
    'COPYING.TXT': 'COPYING',
    'WORMGPL.TXT': 'apps/vendor/gemworm/license.txt',
    'KILOLIC.TXT': 'apps/vendor/kilo/LICENSE',
    'PUZZLIC.TXT': 'apps/vendor/puzzles/LICENCE',
    'STBLIC.TXT': 'apps/vendor/stb/LICENSE',
    'TINYLIC.TXT': 'apps/vendor/tinyexpr/LICENSE',
    'NEWLIB.TXT': 'apps/vendor/newlib/COPYING.NEWLIB',
    'SSHGPL.TXT': 'apps/vendor/wolfssl/COPYING',
    'SSHLICE.TXT': 'apps/vendor/wolfssh/LICENSING',
    'CRYPTLIC.TXT': 'apps/vendor/wolfssl/LICENSING',
    'VTERMLIC.TXT': 'apps/vendor/libvterm/LICENSE',
    'BCRYPT.TXT': 'apps/vendor/bcrypt/COPYING',
    'MP3LIC.TXT': 'apps/vendor/minimp3/LICENSE',
}

def stage(root, dest):
    for folder in APP_GROUPS:
        (dest/folder).mkdir(parents=True, exist_ok=True)
    # AES discovers startup accessories at the boot volume's root.
    for accessory in ('CALC', 'CLOCK', 'CONTROL', 'MONITOR', 'VMUTOOL'):
        shutil.copy2(root/'build/apps'/f'{accessory}.ACC', dest)
    for tool in CLI_TOOLS + NET_TOOLS + SSH_TOOLS:
        shutil.copy2(root/'build/apps'/f'{tool}.TTP', dest/'UTILS')
    for folder, apps in APP_GROUPS.items():
        for app in apps:
            shutil.copy2(root/'build/apps'/f'{app}.PRG', dest/folder)
    # Deterministic 256 KiB input for the read-only file throughput test.
    (dest/'UTILS/BENCH.DAT').write_bytes(bytes(range(256)) * 1024)
    for name, source in NOTICES.items():
        shutil.copy2(root/source,dest/name)

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path);p.add_argument('destination',type=Path)
    a=p.parse_args();stage(a.root,a.destination)
