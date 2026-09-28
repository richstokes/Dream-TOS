#!/usr/bin/env python3
"""Build the fixed 4 MiB FAT16 read-only D: volume from a directory of 8.3 files."""
import argparse
import struct
from pathlib import Path
SECTOR=512
SECTORS=8192
FAT_SECTORS=32
ROOT_ENTRIES=128
DATA_START=73

def build(source):
    image=bytearray(SECTOR*SECTORS)
    image[:11]=b'\xeb\x3c\x90EMUTOSDC'
    struct.pack_into('<HBHBHHBHHHII',image,11,512,1,1,2,128,8192,0xf8,32,32,64,0,0)
    image[38]=0x29
    struct.pack_into('<I',image,39,0x44434554)
    image[43:54]=b'EMUTOS DISC'
    image[54:62]=b'FAT16   '
    image[510:512]=b'\x55\xaa'
    fat=bytearray(FAT_SECTORS*SECTOR)
    struct.pack_into('<HH',fat,0,0xfff8,0xffff)
    cluster=2
    names=set()
    files=sorted(Path(source).iterdir())
    if len(files)>ROOT_ENTRIES:raise ValueError('too many files')
    for i,path in enumerate(files):
        if not path.is_file() or path.is_symlink():raise ValueError(f'only regular root files supported: {path}')
        parts=path.name.upper().split('.')
        if len(parts)>2 or not 1<=len(parts[0])<=8 or (len(parts)==2 and not 1<=len(parts[1])<=3):
            raise ValueError(f'8.3 filename required: {path.name}')
        name=(parts[0].ljust(8)+(parts[1] if len(parts)==2 else '').ljust(3)).encode('ascii')
        if any(c not in b' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-$~!#%&()@^`{}' for c in name):raise ValueError('invalid FAT name')
        if name in names:raise ValueError('duplicate case-insensitive filename')
        names.add(name)
        data=path.read_bytes();count=(len(data)+511)//512
        if cluster+count>SECTORS-DATA_START+2:raise ValueError('volume full')
        entry=(1+2*FAT_SECTORS)*SECTOR+i*32
        image[entry:entry+11]=name;image[entry+11]=0x21 # read-only + archive
        struct.pack_into('<HHHI',image,entry+22,0,((2026-1980)<<9)|(9<<5)|28,cluster if count else 0,len(data))
        pos=(DATA_START+cluster-2)*SECTOR;image[pos:pos+len(data)]=data
        for n in range(count):struct.pack_into('<H',fat,(cluster+n)*2,0xffff if n==count-1 else cluster+n+1)
        cluster+=count
    for i in range(2):image[(1+i*FAT_SECTORS)*SECTOR:(1+(i+1)*FAT_SECTORS)*SECTOR]=fat
    return image

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_bytes(build(a.directory))
