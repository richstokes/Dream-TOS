#!/usr/bin/env python3
"""Build the fixed 4 MiB FAT16 read-only D: volume from an 8.3 directory tree."""
import argparse
import struct
from pathlib import Path

SECTOR = 512
SECTORS = 8192
FAT_SECTORS = 32
ROOT_ENTRIES = 128
DATA_START = 73


def short_name(path):
    parts = path.name.upper().split('.')
    if (len(parts) > 2 or not 1 <= len(parts[0]) <= 8 or
            (len(parts) == 2 and not 1 <= len(parts[1]) <= 3)):
        raise ValueError(f'8.3 filename required: {path.name}')
    allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-$~!#%&()@^`{}"
    if any(c not in allowed for part in parts for c in part):
        raise ValueError(f'invalid FAT name: {path.name}')
    return (parts[0].ljust(8) + (parts[1] if len(parts) == 2 else '').ljust(3)).encode('ascii')


def build(source):
    image = bytearray(SECTOR * SECTORS)
    image[:11] = b'\xeb\x3c\x90EMUTOSDC'
    struct.pack_into('<HBHBHHBHHHII', image, 11, 512, 1, 1, 2, 128, 8192, 0xf8, 32, 32, 64, 0, 0)
    image[38] = 0x29
    struct.pack_into('<I', image, 39, 0x44434554)
    image[43:54] = b'DREAM TOS  '
    image[54:62] = b'FAT16   '
    image[510:512] = b'\x55\xaa'
    fat = bytearray(FAT_SECTORS * SECTOR)
    struct.pack_into('<HH', fat, 0, 0xfff8, 0xffff)
    next_cluster = 2

    def allocate(size):
        nonlocal next_cluster
        count = (size + SECTOR - 1) // SECTOR
        if not count:
            return 0
        if next_cluster + count > SECTORS - DATA_START + 2:
            raise ValueError('volume full')
        first = next_cluster
        for cluster in range(first, first + count):
            struct.pack_into('<H', fat, cluster * 2,
                             0xffff if cluster == first + count - 1 else cluster + 1)
        next_cluster += count
        return first

    def position(cluster):
        return (DATA_START + cluster - 2) * SECTOR

    def entry(name, attr, cluster, size=0):
        record = bytearray(32)
        record[:11] = name
        record[11] = attr
        struct.pack_into('<HHHI', record, 22, 0, ((2026 - 1980) << 9) | (9 << 5) | 28,
                         cluster, size)
        return record

    def directory(path, parent_cluster=0, root=False):
        children = sorted(path.iterdir())
        if root and len(children) > ROOT_ENTRIES:
            raise ValueError('too many root entries')
        cluster = 0 if root else allocate((len(children) + 2) * 32)
        records = bytearray()
        if not root:
            records += entry(b'.          ', 0x10, cluster)
            records += entry(b'..         ', 0x10, parent_cluster)
        names = set()
        for child in children:
            if child.is_symlink() or not (child.is_file() or child.is_dir()):
                raise ValueError(f'only regular files and directories supported: {child}')
            name = short_name(child)
            if name in names:
                raise ValueError(f'duplicate case-insensitive filename: {child}')
            names.add(name)
            if child.is_dir():
                records += entry(name, 0x10, directory(child, cluster))
            else:
                data = child.read_bytes()
                first = allocate(len(data))
                if data:
                    pos = position(first)
                    image[pos:pos + len(data)] = data
                records += entry(name, 0x21, first, len(data))  # read-only + archive
        pos = (1 + 2 * FAT_SECTORS) * SECTOR if root else position(cluster)
        image[pos:pos + len(records)] = records
        return cluster

    directory(Path(source), root=True)
    for i in range(2):
        image[(1 + i * FAT_SECTORS) * SECTOR:(1 + (i + 1) * FAT_SECTORS) * SECTOR] = fat
    return image


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_bytes(build(args.directory))
