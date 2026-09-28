#!/usr/bin/env python3
"""Create synthetic VMU images for read-only toolbox testing in Flycast."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

def image(files):
    data=bytearray(256*512)
    root=255*512;fat=254*512;directory=253*512
    data[root:root+16]=b'\x55'*16
    struct.pack_into('<6H',data,root+70,254,1,253,13,0,200)
    for i in range(256):struct.pack_into('<H',data,fat+2*i,0xfffc if i<200 else 0xfffa)
    first=0
    for i in range(files):
        count=i%3+1;entry=directory+32*i
        data[entry]=0x33;data[entry+1]=0xff if i%4==0 else 0
        struct.pack_into('<H',data,entry+2,first)
        data[entry+4:entry+16]=f'NOTE{i+1:02d}.TXT'.encode().ljust(12,b' ')
        data[entry+16:entry+24]=bytes([0x20,0x26,0x09,0x28,0x12,(i//10)*16+i%10,0,0])
        struct.pack_into('<HH',data,entry+24,count,0)
        for n in range(count):struct.pack_into('<H',data,fat+2*(first+n),first+n+1 if n+1<count else 0xfffa)
        first+=count
    return data

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);a=p.parse_args()
    a.directory.mkdir(parents=True,exist_ok=True)
    hashes={}
    for name,files in [('vmu_save_A1.bin',12),('vmu_save_A2.bin',0)]:
        target=a.directory/name
        with target.open('xb') as out:out.write(image(files))
        hashes[name]=hashlib.sha256(target.read_bytes()).hexdigest()
    (a.directory/'SHA256.json').write_text(json.dumps(hashes,indent=2)+'\n')
    print('Created populated A1 and empty A2 fixtures in',a.directory)
