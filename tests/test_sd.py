"""SD card volume discovery (MBR, FAT12/16 boot sectors) under ASan/UBSan,
using synthetic card images and, where available, the host's own formatter."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

def boot_sector(sectors, spc, reserved=1, nfats=2, rootent=512, fatsz=None, fat32=False, media=0xf8):
    """A minimal valid FAT12/16 boot sector, sized like a real formatter's output."""
    rdlen = (rootent * 32 + 511) // 512
    if fatsz is None:
        fatsz = 1
        while True:  # smallest FAT that covers the resulting clusters
            n = (sectors - reserved - nfats * fatsz - rdlen) // spc
            need = ((n + 2) * 2 if n >= 4085 else ((n + 2) * 3 + 1) // 2 + 511)
            if fatsz * 512 >= need: break
            fatsz += 1
    b = bytearray(512)
    b[0:3] = b'\xeb\x3c\x90'; b[3:11] = b'MSDOS5.0'
    struct.pack_into('<HBHBH', b, 11, 512, spc, reserved, nfats, rootent)
    struct.pack_into('<HBH', b, 19, sectors if sectors < 65536 else 0, media, 0 if fat32 else fatsz)
    struct.pack_into('<I', b, 32, 0 if sectors < 65536 else sectors)
    if fat32: struct.pack_into('<I', b, 36, 1000); b[17:19] = b'\0\0'
    b[510:512] = b'\x55\xaa'
    return bytes(b), fatsz, rdlen

def mbr(*parts):
    b = bytearray(512)
    for i, (ptype, start, size) in enumerate(parts):
        struct.pack_into('<B3sB3sII', b, 446 + 16 * i, 0, b'\0\0\0', ptype, b'\0\0\0', start, size)
    b[510:512] = b'\x55\xaa'
    return b

class SdScan(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls.tmp.name)
        cls.exe = cls.dir / 'sd_scan'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Iinclude',
            'tests/sd_scan_host.c', 'src/dreamcast/sd_fat.c', '-o', str(cls.exe)], cwd=ROOT, check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def scan(self, image, *args):
        path = self.dir / 'card.img'
        path.write_bytes(image)
        r = subprocess.run([str(self.exe), str(path), *map(str, args)], capture_output=True, timeout=10)
        out = r.stdout.decode() + r.stderr.decode()
        self.assertNotIn('Sanitizer', out)
        return r.returncode, out

    def image(self, total, parts, contents):
        img = bytearray(total * 512)
        img[0:512] = mbr(*parts)
        for start, sector in contents: img[start * 512:start * 512 + 512] = sector
        return bytes(img)

    def test_fat16_partition_geometry(self):
        part = 40000
        boot, fatsz, rdlen = boot_sector(part, 4, reserved=4)
        code, out = self.scan(self.image(part + 2048, [(0x06, 2048, part)], [(2048, boot)]))
        self.assertEqual(code, 0)
        self.assertIn('count 1 skipped 0', out)
        numcl = (part - 4 - 2 * fatsz - rdlen) // 4
        self.assertIn(f'vol 0 start=2048 sectors={part} clsiz=4 rdlen={rdlen} fsiz={fatsz} '
                      f'fatrec={4 + fatsz} datrec={4 + 2 * fatsz + rdlen} numcl={numcl} fat16=1 nfats=2', out)

    def test_fat12_single_fat_and_large_total_field(self):
        boot, fatsz, rdlen = boot_sector(2880, 1, nfats=1, rootent=224, media=0xf0)
        code, out = self.scan(self.image(4096, [(0x01, 63, 2880)], [(63, boot)]))
        self.assertIn(f'fatrec=1 datrec={1 + fatsz + rdlen} numcl={(2880 - 1 - fatsz - rdlen)} fat16=0 nfats=1', out)
        boot, fatsz, rdlen = boot_sector(500000, 16)  # 32-bit sector count, 0x0e = FAT16 LBA
        code, out = self.scan(self.image(500064, [(0x0e, 32, 500000)], [(32, boot)]))
        self.assertIn('count 1 skipped 0', out); self.assertIn('sectors=500000 clsiz=16', out)

    def test_bare_volume_without_mbr(self):
        boot, *_ = boot_sector(20000, 2)
        code, out = self.scan(boot + bytes(19999 * 512))
        self.assertIn('count 1 skipped 0', out); self.assertIn('vol 0 start=0', out)

    def test_up_to_four_partitions_and_ignored_types(self):
        boot, *_ = boot_sector(8192, 2)
        parts = [(0x06, 64, 8192), (0x83, 8256, 100), (0x06, 8400, 8192), (0x05, 16600, 100)]
        img = self.image(17000, parts, [(64, boot), (8400, boot)])
        code, out = self.scan(img)
        self.assertIn('count 2 skipped 0', out)
        self.assertIn('vol 0 start=64 ', out); self.assertIn('vol 1 start=8400 ', out)

    def test_fat32_is_reported_not_mounted(self):
        f32, *_ = boot_sector(100000, 8, fat32=True)
        code, out = self.scan(self.image(100100, [(0x0c, 32, 100000)], [(32, f32)]))
        self.assertIn('count 0 skipped 1', out)
        # A FAT32 volume mislabelled as FAT16 is still recognised from its BPB.
        code, out = self.scan(self.image(100100, [(0x06, 32, 100000)], [(32, f32)]))
        self.assertIn('count 0 skipped 1', out)
        code, out = self.scan(f32 + bytes(99999 * 512))
        self.assertIn('count 0 skipped 1', out)

    def test_damaged_and_unsupported_volumes(self):
        good, *_ = boot_sector(8192, 2)
        bad_size = bytearray(good); bad_size[11:13] = struct.pack('<H', 1024)
        no_sig = bytearray(good); no_sig[510] = 0
        too_big = boot_sector(8192, 2)[0]  # claims more sectors than its partition holds
        cases = {'sector size': bytes(bad_size), 'signature': bytes(no_sig)}
        for name, sector in cases.items():
            code, out = self.scan(self.image(9000, [(0x06, 64, 8192)], [(64, sector)]))
            self.assertIn('count 0 skipped 2', out, name)
        code, out = self.scan(self.image(9000, [(0x06, 64, 4000)], [(64, too_big)]))
        self.assertIn('count 0 skipped 2', out)
        code, out = self.scan(self.image(9000, [(0x06, 8500, 8192)], [(64, good)]))  # beyond card end
        self.assertIn('count 0 skipped 2', out)
        code, out = self.scan(bytes(9000 * 512))  # blank card
        self.assertIn('count 0 skipped 0', out)
        for tweak in ((16, 0), (16, 3), (13, 0), (13, 3), (13, 128), (14, 0), (17, 0)):
            broken = bytearray(good); broken[tweak[0]:tweak[0] + 1 if tweak[0] != 17 else 19] = bytes([tweak[1]]) * (1 if tweak[0] != 17 else 2)
            code, out = self.scan(self.image(9000, [(0x06, 64, 8192)], [(64, bytes(broken))]))
            self.assertIn('count 0 skipped 2', out, tweak)

    def test_read_failure_and_limits(self):
        good, *_ = boot_sector(8192, 2)
        img = self.image(9000, [(0x06, 64, 8192)], [(64, good)])
        code, out = self.scan(img, 0)
        self.assertIn('count -1', out)
        code, out = self.scan(img, 64)
        self.assertIn('count 0 skipped 2', out)
        # 12-bit FAT that is too small for its clusters, and reserved area beyond 16 bits.
        small_fat, *_ = boot_sector(8192, 1, fatsz=1)
        code, out = self.scan(self.image(9000, [(0x06, 64, 8192)], [(64, small_fat)]))
        self.assertIn('count 0 skipped 2', out)
        huge_reserved, *_ = boot_sector(200000, 4, reserved=65000, fatsz=600)
        code, out = self.scan(self.image(200100, [(0x06, 64, 200000)], [(64, huge_reserved)]))
        self.assertIn('count 0 skipped 2', out)

    @unittest.skipUnless(shutil.which('newfs_msdos'), 'needs the macOS formatter')
    def test_matches_host_formatter(self):
        for size_mb, fat, spc in ((16, 16, 0), (32, 16, 4), (100, 16, 0), (2, 12, 1)):
            path = self.dir / 'fmt.img'
            path.write_bytes(bytes(size_mb * 1024 * 1024))
            cmd = ['newfs_msdos', '-F', str(fat), '-s', str(size_mb * 2048)] + (['-c', str(spc)] if spc else []) + [str(path)]
            made = subprocess.run(cmd, capture_output=True)
            if made.returncode: self.skipTest('formatter refused: ' + made.stderr.decode())
            data = path.read_bytes()
            reserved, nfats, rootent = struct.unpack_from('<HBH', data, 14)[0], data[16], struct.unpack_from('<H', data, 17)[0]
            fatsz, total = struct.unpack_from('<H', data, 22)[0], struct.unpack_from('<H', data, 19)[0] or struct.unpack_from('<I', data, 32)[0]
            rdlen = (rootent * 32 + 511) // 512
            code, out = self.scan(data)
            self.assertIn('count 1', out, out)
            self.assertIn(f'sectors={total} clsiz={data[13]} rdlen={rdlen} fsiz={fatsz} '
                          f'fatrec={reserved + (fatsz if nfats > 1 else 0)} datrec={reserved + nfats * fatsz + rdlen}', out)
            self.assertIn(f'fat16={1 if fat == 16 else 0}', out)

if __name__ == '__main__':
    unittest.main()
