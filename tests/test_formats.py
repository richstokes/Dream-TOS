"""Host checks for independently readable FAT volumes and native app metadata."""
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from fat_reader import FatVolume
ROOT=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/f'{name}.py')
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod
fat=module('fat_image');native=module('native_app')
class Formats(unittest.TestCase):
    def test_fat_multicluster_roundtrip(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);payload=bytes(range(256))*9+b'end'
            (p/'PAYLOAD.BIN').write_bytes(payload);(p/'EMPTY.TXT').touch()
            image=fat.build(p)
        self.assertEqual(len(image),4*1024*1024)
        self.assertEqual(image[510:512],b'\x55\xaa')
        self.assertEqual(image[512:33*512],image[33*512:65*512])
        root=image[65*512:73*512]
        e=root[32:64];self.assertEqual(e[:11],b'PAYLOAD BIN')
        cluster=struct.unpack_from('<H',e,26)[0];out=bytearray();seen=set()
        while cluster<0xfff8:
            self.assertGreaterEqual(cluster,2);self.assertNotIn(cluster,seen);seen.add(cluster)
            pos=(73+cluster-2)*512;out+=image[pos:pos+512]
            cluster=struct.unpack_from('<H',image,512+cluster*2)[0]
        size=struct.unpack_from('<I',e,28)[0]
        self.assertEqual(out[:size],payload)
        self.assertEqual(struct.unpack_from('<HI',root,26),(0,0))
    def test_nested_directories_and_multicluster_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root/'APPS/EMPTY').mkdir(parents=True)
            (root/'APPS/SUB').mkdir()
            payload = bytes(range(256)) * 9 + b'end'
            (root/'APPS/SUB/DATA.BIN').write_bytes(payload)
            for i in range(20):
                (root/f'APPS/FILE{i:02}.TXT').write_text(str(i))
            image = fat.build(root)
            self.assertEqual(image, fat.build(root))
        volume = FatVolume(image)
        self.assertEqual(volume.read('APPS/SUB/DATA.BIN'), payload)
        apps = volume.lookup('APPS')[1]
        self.assertEqual(len(volume.chain(apps)), 1024)
        entries = volume.entries(apps)
        self.assertEqual(entries['.'], (0x10, apps, 0))
        self.assertEqual(entries['..'], (0x10, 0, 0))
        sub = volume.lookup('APPS/SUB')[1]
        self.assertEqual(volume.entries(sub)['..'], (0x10, apps, 0))
        empty = volume.lookup('APPS/EMPTY')[1]
        self.assertEqual(set(volume.entries(empty)), {'.', '..'})
        for i in range(20):
            self.assertEqual(volume.read(f'APPS/FILE{i:02}.TXT'), str(i).encode())
        self.assertEqual(image[512:33*512], image[33*512:65*512])

    def test_grouped_programs_are_packaged(self):
        volume = FatVolume((ROOT/'build/disc/DISC.IMG').read_bytes())
        expected = {
            'APPS': {'EDITOR', 'IMAGES', 'PAINT', 'MP3'},
            'GAMES': {'FIFTEEN', 'MINES', 'NET', 'WORM', 'BLOCKS'},
            'UTILS': {'FTP', 'VMUEDIT', 'SYSINFO', 'BENCH', 'HELLO', 'VDITEST', 'RUNTIME'},
        }
        self.assertFalse(any(name.endswith(('.PRG', '.TTP')) for name in volume.entries()))
        for folder, names in expected.items():
            attr, cluster, size = volume.lookup(folder)
            self.assertEqual(attr, 0x10)
            self.assertEqual(size, 0)
            programs = {name[:-4] for name in volume.entries(cluster) if name.endswith('.PRG')}
            self.assertNotIn('CALC', programs)
            self.assertLessEqual(names, programs)
            for name in programs:
                self.assertEqual(volume.read(f'{folder}/{name}.PRG'),
                                 (ROOT/f'build/apps/{name}.PRG').read_bytes())
        for name in ('PING', 'NSLOOKUP', 'IFCONFIG'):
            self.assertEqual(volume.read(f'UTILS/{name}.TTP'),
                             (ROOT/f'build/apps/{name}.TTP').read_bytes())
        self.assertEqual(volume.read('UTILS/BENCH.DAT'), bytes(range(256)) * 1024)

    def test_reject_nested_symlinks_and_invalid_names(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)/'APPS'
            folder.mkdir()
            (folder/'BACK').symlink_to(temp, target_is_directory=True)
            with self.assertRaises(ValueError):
                fat.build(temp)
            (folder/'BACK').unlink()
            (folder/'LONG-FILENAME.PRG').touch()
            with self.assertRaises(ValueError):
                fat.build(temp)
        with tempfile.TemporaryDirectory() as temp:
            a, b = Path(temp)/'APP.PRG', Path(temp)/'app.prg'
            a.touch(); b.touch()
            with patch.object(Path, 'iterdir', return_value=iter([a, b])):
                with self.assertRaises(ValueError):
                    fat.build(temp)

    def test_reject_full_root_and_volume(self):
        with tempfile.TemporaryDirectory() as temp:
            for i in range(129):
                (Path(temp)/f'{i}.TXT').touch()
            with self.assertRaisesRegex(ValueError, 'root entries'):
                fat.build(temp)
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp)/'APPS').mkdir()
            # The file fits by itself, but its parent directory needs a cluster too.
            (Path(temp)/'APPS/FULL.DAT').write_bytes(bytes((8192 - 73) * 512))
            with self.assertRaisesRegex(ValueError, 'volume full'):
                fat.build(temp)

    def test_native_accessories_are_packaged(self):
        image=(ROOT/'build/disc/DISC.IMG').read_bytes()
        entries=[image[i:i+32] for i in range(65*512,73*512,32)]
        for name in ('CALC', 'CLOCK', 'CONTROL', 'MONITOR', 'VMUTOOL'):
            with self.subTest(accessory=name):
                expected=native.convert((ROOT/f'build/apps/{name}.elf').read_bytes())
                self.assertEqual((ROOT/f'build/apps/{name}.ACC').read_bytes(),expected)
                entry=next(e for e in entries if e[:11]==f'{name:<8}ACC'.encode())
                cluster=struct.unpack_from('<H',entry,26)[0]
                size=struct.unpack_from('<I',entry,28)[0]
                payload=bytearray();seen=set()
                while cluster<0xfff8:
                    self.assertGreaterEqual(cluster,2)
                    self.assertNotIn(cluster,seen);seen.add(cluster)
                    pos=(73+cluster-2)*512;payload+=image[pos:pos+512]
                    cluster=struct.unpack_from('<H',image,512+cluster*2)[0]
                self.assertEqual(payload[:size],expected)

    def test_reject_bad_names(self):
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp)/'too-long-name.txt').touch()
            with self.assertRaises(ValueError):fat.build(temp)
    def test_reject_foreign_binary(self):
        with self.assertRaises(ValueError):native.convert(b'\x60\x1a'+bytes(100))
    def test_native_application_layout(self):
        raw=(ROOT/'build/apps/hello.elf').read_bytes();program=native.convert(raw)
        magic,abi,size,mem,entry,count,reserved=struct.unpack_from('<8s6I',program)
        self.assertEqual(magic,b'DCNATIVE');self.assertEqual(abi,1)
        self.assertGreater(mem,size);self.assertLess(entry,size);self.assertEqual(reserved,0)
        self.assertEqual(len(program),32+size+4*count);self.assertGreater(count,0)
        offsets=struct.unpack_from(f'<{count}I',program,32+size)
        self.assertEqual(list(offsets),sorted(set(offsets)))
        for offset in offsets:
            self.assertEqual(offset%4,0)
            self.assertLessEqual(struct.unpack_from('<I',program,32+offset)[0],mem)
if __name__=='__main__':unittest.main()
