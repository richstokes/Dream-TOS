#!/usr/bin/env python3
"""Create synthetic VMU images for toolbox testing in Flycast, and provide a
writable in-memory card model (VmuCard) for host tests of the file service.

VmuCard mirrors the KallistiOS vmufs on-card rules (data files allocated from
the top block downwards, delete-then-allocate overwrite, FAT written before
the directory) and includes an independent consistency checker. It is a test
oracle, not an OS component: nothing in the OS can format a card."""
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

FAT_FREE=0xfffc;FAT_END=0xfffa
FIXED_STAMP=bytes([0x20,0x26,0x09,0x29,0x12,0x00,0x00,0x01])

class VmuCard:
    """Standard 128 KiB VMU: root block 255, FAT 254, directory 253 downwards."""
    def __init__(self,data=None):
        self.data=bytearray(data) if data is not None else image(0)
        assert len(self.data)==256*512
    # --- layout ---
    def u16(self,off):return struct.unpack_from('<H',self.data,off)[0]
    @property
    def fat_loc(self):return self.u16(255*512+70)
    @property
    def dir_loc(self):return self.u16(255*512+74)
    @property
    def dir_size(self):return self.u16(255*512+76)
    @property
    def blk_cnt(self):return self.u16(255*512+80)
    def fat_get(self,block):return self.u16(self.fat_loc*512+2*block)
    def fat_set(self,block,value):struct.pack_into('<H',self.data,self.fat_loc*512+2*block,value)
    def entry_offset(self,index):return (self.dir_loc-index//16)*512+32*(index%16)
    def entry(self,index):
        o=self.entry_offset(index);d=self.data
        return dict(index=index,type=d[o],protect=d[o+1],first=self.u16(o+2),name=bytes(d[o+4:o+16]),
            blocks=self.u16(o+24),header=self.u16(o+26))
    def entries(self):
        return [e for e in (self.entry(i) for i in range(self.dir_size*16)) if e['type']]
    def free_blocks(self):return sum(1 for b in range(self.blk_cnt) if self.fat_get(b)==FAT_FREE)
    def chain(self,e):
        out=[];b=e['first']
        for _ in range(e['blocks']):
            out.append(b);b=self.fat_get(b)
        assert b==FAT_END,'chain does not end after the recorded length'
        return out
    def read(self,name):
        for e in self.entries():
            if e['name'].rstrip(b'\0 ')==name.rstrip(b'\0 '):
                return b''.join(self.data[c*512:c*512+512] for c in self.chain(e))
        raise KeyError(name)
    # --- independent consistency check ---
    def fsck(self):
        """Return a list of problems (empty when the card is consistent)."""
        problems=[];d=self.data
        if d[255*512:255*512+16]!=b'\x55'*16:problems.append('bad root magic')
        owner={}
        for e in self.entries():
            if e['type'] not in (0x33,0xcc):problems.append(f"entry {e['index']}: bad type");continue
            b=e['first'];n=0
            while n<e['blocks']:
                if b>=self.blk_cnt:problems.append(f"entry {e['index']}: block {b} out of range");break
                if b in owner:problems.append(f"block {b} shared by entries {owner[b]} and {e['index']}");break
                owner[b]=e['index'];b=self.fat_get(b);n+=1
            else:
                if b!=FAT_END:problems.append(f"entry {e['index']}: chain does not end")
        names={}
        for e in self.entries():
            key=e['name'].split(b'\0')[0].rstrip(b' ')
            if key in names:problems.append(f"duplicate name {key!r}")
            names[key]=1
        for b in range(self.blk_cnt):
            v=self.fat_get(b)
            if v==FAT_FREE and b in owner:problems.append(f'block {b} is free but owned')
            if v!=FAT_FREE and b not in owner:problems.append(f'block {b} leaked (in FAT, not in any file)')
        return problems
    # --- KOS-equivalent mutation (fn is the exact name KOS receives) ---
    @staticmethod
    def _strncmp_equal(fn,stored):
        """C strncmp(fn, stored, 12)==0 with fn a C string."""
        fn=fn.split(b'\0')[0]
        for i in range(12):
            a=fn[i] if i<len(fn) else 0
            if a!=stored[i]:return False
            if a==0:return True
        return True
    def _find(self,fn):
        for e in self.entries():
            if self._strncmp_equal(fn,e['name']):return e['index']
        return -1
    def _blank_entry(self,index):
        self.data[self.entry_offset(index):self.entry_offset(index)+32]=bytes(32)
    def _find_block(self):
        for b in range(self.blk_cnt-1,-1,-1):
            if self.fat_get(b)==FAT_FREE:return b
        raise RuntimeError('no free block')
    def kos_write(self,fn,data,overwrite=False):
        """Returns 0, or KOS's error code: -2 exists, -7 no space."""
        data=bytes(data);data+=bytes((-len(data))%512) if data else bytes(512)
        idx=self._find(fn)
        if idx>=0:
            if not overwrite:return -2
            for b in self.chain(self.entry(idx)):self.fat_set(b,FAT_FREE)
            self._blank_entry(idx)
        blocks=len(data)//512
        if self.free_blocks()<blocks:return -7
        cur=first=self._find_block();prev=None;n=0
        for n in range(blocks):
            self.data[cur*512:cur*512+512]=data[n*512:n*512+512]
            if n+1<blocks:
                self.fat_set(cur,FAT_END);nxt=self._find_block();self.fat_set(cur,nxt);cur=nxt
            else:self.fat_set(cur,FAT_END)
        slot=next(i for i in range(self.dir_size*16) if self.data[self.entry_offset(i)]==0)
        o=self.entry_offset(slot);raw=fn.split(b'\0')[0][:12].ljust(12,b'\0')
        self.data[o:o+32]=bytes([0x33,0])+struct.pack('<H',first)+raw+FIXED_STAMP+struct.pack('<HH',blocks,0)+bytes(4)
        return 0
    def kos_delete(self,fn):
        idx=self._find(fn)
        if idx<0:return -1
        for b in self.chain(self.entry(idx)):self.fat_set(b,FAT_FREE)
        self._blank_entry(idx);return 0
    def masked(self):
        """Image with directory timestamps zeroed, for comparing to real KOS output."""
        out=bytearray(self.data)
        for i in range(self.dir_size*16):
            o=self.entry_offset(i);out[o+16:o+24]=bytes(8)
        return bytes(out)

def crc16(data):
    """CRC-16/CCITT (XMODEM): polynomial 0x1021, initial value 0, as KOS net_crc16ccitt."""
    crc=0
    for byte in data:
        crc^=byte<<8
        for _ in range(8):crc=((crc<<1)^0x1021 if crc&0x8000 else crc<<1)&0xffff
    return crc

def vms_save(short,long_,app,frames,palette,payload,speed=8):
    """A standard VMS save file: 128-byte header, 512 bytes per 32x32 4-bit icon
    frame (left pixel in the high nibble, top row first), then the data.
    The CRC covers header, icons and data with the CRC field zeroed."""
    assert 1<=len(frames)<=3 and all(len(f)==512 for f in frames) and len(palette)==16
    header=bytearray(128)
    header[0:16]=short.encode().ljust(16);header[16:48]=long_.encode().ljust(32)
    header[48:64]=app.encode().ljust(16,b'\0')
    struct.pack_into('<HHHHI',header,64,len(frames),speed,0,0,len(payload))
    for i,colour in enumerate(palette):struct.pack_into('<H',header,96+2*i,colour)
    body=bytes(header)+b''.join(frames)+payload
    struct.pack_into('<H',header,70,crc16(body))
    return bytes(header)+b''.join(frames)+payload

def icon_frame(pattern):
    """512 icon bytes from a function (x, y) -> palette index."""
    out=bytearray(512)
    for y in range(32):
        for x in range(32):
            c=pattern(x,y)&15;o=y*16+x//2
            out[o]|=c<<4 if x%2==0 else c
    return bytes(out)

def editor_card():
    """A card for exercising VMUEDIT: VMS saves with icons, a raw file, a protected file and a game."""
    card=VmuCard(image(0))
    palette=[0x0000,0xf000,0xfd22,0xf2d2,0xf22d,0xfdd2,0xf888,0xffff]+[0xf000|(i*0x111&0xfff) for i in range(8)]
    def ring(x,y):
        d=(x-16)**2+(y-16)**2
        return 1 if 100<d<150 else (2+x//11 if d<=100 else 0)
    card.kos_write(b'ICONTEST',vms_save('Icon test','A one-frame VMS save for VMUEDIT','VMUFIX',[icon_frame(ring)],palette,bytes(range(200))))
    frames=[icon_frame(lambda x,y,k=k:(x+y+k*8)//4%8) for k in range(3)]
    card.kos_write(b'ANIMATED.001',vms_save('Animated','Three icon frames','VMUFIX',frames,palette,b'anim'*300))
    card.kos_write(b'RAWDATA.BIN',bytes(i*7&255 for i in range(2000)))
    card.kos_write(b'LOCKED',bytes(700))
    card.kos_write(b'MINIGAME',bytes(900))
    for e in card.entries():
        o=card.entry_offset(e['index'])
        if e['name'].startswith(b'LOCKED'):card.data[o+1]=0xff
        if e['name'].startswith(b'MINIGAME'):card.data[o]=0xcc
    return card

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    p.add_argument('--editor',action='store_true',help='card A1 holds VMS saves with icons, a raw file, a protected file and a game, for VMUEDIT (which changes it)')
    a=p.parse_args()
    a.directory.mkdir(parents=True,exist_ok=True)
    hashes={}
    contents=[('vmu_save_A1.bin',editor_card().data if a.editor else image(12)),('vmu_save_A2.bin',image(0))]
    for name,data in contents:
        target=a.directory/name
        with target.open('xb') as out:out.write(data)
        hashes[name]=hashlib.sha256(target.read_bytes()).hexdigest()
    (a.directory/'SHA256.json').write_text(json.dumps(hashes,indent=2)+'\n')
    print('Created',('editor test card A1 and empty A2' if a.editor else 'populated A1 and empty A2 fixtures'),'in',a.directory)
