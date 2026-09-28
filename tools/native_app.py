#!/usr/bin/env python3
"""Convert a zero-linked SH-4 ELF with --emit-relocs to our native .PRG.
Only resolved absolute R_SH_DIR32 relocations are accepted. No third-party deps.
"""
import argparse
import struct
from pathlib import Path

def convert(raw):
    if raw[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('expected ELF32 little endian')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', raw)
    if h[1] != 2 or h[2] != 42 or h[11] != 40:
        raise ValueError('expected linked SuperH ELF with section headers')
    sections = [struct.unpack_from('<10I', raw, h[6]+i*40) for i in range(h[12])]
    allocated = [(i,s) for i,s in enumerate(sections) if s[2]&2 and s[5]]
    size = max(s[3]+s[5] for _,s in allocated if s[1]!=8)
    memory = max(s[3]+s[5] for _,s in allocated)
    if min(s[3] for _,s in allocated)!=0 or memory>2*1024*1024 or h[4]>=size:
        raise ValueError('invalid load address, memory size or entry point')
    image = bytearray(size)
    for _,s in allocated:
        if s[1]!=8: image[s[3]:s[3]+s[5]] = raw[s[4]:s[4]+s[5]]
    relocs=[]
    for s in sections:
        if s[1]!=4 or not sections[s[7]][2]&2: continue
        for pos in range(s[4],s[4]+s[5],s[9]):
            offset,info,addend=struct.unpack_from('<IIi',raw,pos)
            # R_SH_NONE marks linker alignment/relaxation bookkeeping.
            if info&255 == 0: continue
            if info&255 != 1: raise ValueError(f'unsupported SH relocation {info&255}')
            if offset&3 or offset+4>size: raise ValueError('unaligned/out-of-range DIR32')
            if struct.unpack_from('<I',image,offset)[0]>memory:
                raise ValueError('absolute reference outside program image')
            relocs.append(offset)
    if len(relocs)!=len(set(relocs)):raise ValueError('duplicate relocations')
    relocs.sort()
    return (struct.pack('<8s6I',b'DCNATIVE',1,size,memory,h[4],len(relocs),0)+image+
            b''.join(struct.pack('<I',r) for r in relocs))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_bytes(convert(a.elf.read_bytes()))
