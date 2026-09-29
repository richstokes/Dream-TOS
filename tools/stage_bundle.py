#!/usr/bin/env python3
"""Stage exactly the supported native apps and their redistribution notices."""
import argparse
import shutil
from pathlib import Path

APPS = ('HELLO', 'VDITEST', 'CALC', 'EDITOR', 'FIFTEEN', 'MINES', 'NET', 'WORM', 'IMAGES', 'RUNTIME', 'BENCH')
CLI_TOOLS = ('GREP', 'WC', 'HEAD', 'TAIL', 'SORT', 'HEXDUMP', 'CKSUM', 'DATE', 'DF', 'FREE', 'UNAME', 'EXPR')
NOTICES = {
    'COPYING.TXT': 'COPYING',
    'WORMGPL.TXT': 'apps/vendor/gemworm/license.txt',
    'KILOLIC.TXT': 'apps/vendor/kilo/LICENSE',
    'PUZZLIC.TXT': 'apps/vendor/puzzles/LICENCE',
    'STBLIC.TXT': 'apps/vendor/stb/LICENSE',
    'TINYLIC.TXT': 'apps/vendor/tinyexpr/LICENSE',
    'NEWLIB.TXT': 'apps/vendor/newlib/COPYING.NEWLIB',
}

def stage(root, dest):
    for accessory in ('CLOCK', 'CONTROL', 'MONITOR', 'VMUTOOL'):
        shutil.copy2(root/'build/apps'/f'{accessory}.ACC', dest)
    for tool in CLI_TOOLS:
        shutil.copy2(root/'build/apps'/f'{tool}.TTP', dest)
    for app in APPS:
        shutil.copy2(root/'build/apps'/f'{app}.PRG',dest)
    # SYSINFO is maintained alongside the core port and may be built separately.
    if (root/'apps/sysinfo.c').is_file():
        shutil.copy2(root/'build/apps/SYSINFO.PRG',dest)
    # Deterministic 256 KiB input for the read-only file throughput test.
    (dest/'BENCH.DAT').write_bytes(bytes(range(256)) * 1024)
    for name, source in NOTICES.items():
        shutil.copy2(root/source,dest/name)

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path);p.add_argument('destination',type=Path)
    a=p.parse_args();stage(a.root,a.destination)
