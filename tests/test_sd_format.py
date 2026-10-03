"""FAT16 formatting, protection, verification faults and graphical workflow."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class SdFormat(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / 'sd-format'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-Iinclude', 'tests/sd_format_host.c',
            'src/dreamcast/sd_format.c', 'src/dreamcast/sd_service.c', 'src/dreamcast/sd_fat.c',
            '-o', str(cls.exe)], cwd=ROOT, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_card(self, size, mode='ok', *args):
        r = subprocess.run([str(self.exe), str(size), mode, *map(str, args)], capture_output=True, text=True, timeout=20)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return r.stdout

    def test_capacity_and_cluster_boundaries(self):
        for size in [8192, 8193, 16384, 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304, 67108864, 0xffffffff]:
            with self.subTest(sectors=size): self.run_card(size)

    def test_missing_and_small_cards(self):
        for size in [0, 1, 2048, 8191]: self.run_card(size)

    def test_cached_media_requires_reboot(self): self.run_card(65536, 'cached')
    def test_failed_remount_requires_reboot(self): self.run_card(65536, 'mount-error')
    def test_open_files_prevent_all_writes(self): self.run_card(65536, 'busy')

    def test_write_read_and_verification_failures(self):
        out = self.run_card(65536)
        total = int(out.split()[-3])
        for kind in ['write', 'read', 'corrupt']:
            for at in [1, 2, 34, 67, 68, 70, total-1, total]:
                with self.subTest(kind=kind, at=at): self.run_card(65536, f'{kind}:{at}')
        self.run_card(65536, 'read:0')

    def test_independent_fsck(self):
        fsck = shutil.which('fsck.fat') or shutil.which('fsck_msdos') or ('/sbin/fsck_msdos' if Path('/sbin/fsck_msdos').exists() else None)
        if not fsck: self.skipTest('No host FAT checker')
        for size in [8192, 65536, 4194304, 67108864]:
            with self.subTest(sectors=size):
                image = Path(self.tmp.name) / 'volume.img'
                self.run_card(size, 'ok', image)
                r = subprocess.run([fsck, '-n', str(image)], capture_output=True, text=True, timeout=30)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_graphical_workflow(self):
        exe = Path(self.tmp.name) / 'sdformat-ui'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
            '-fsanitize=address,undefined', '-Iinclude', '-Iapps/lib', 'tests/sd_format_ui_host.c',
            'apps/lib/app.c', 'apps/lib/window.c', '-o', str(exe)], cwd=ROOT, check=True)
        subprocess.run([str(exe)], check=True, timeout=10)

    def test_packaging(self):
        import sys
        sys.path.insert(0, str(ROOT / 'tools'))
        import stage_bundle
        self.assertIn('SDFORMAT', stage_bundle.APPS)
        self.assertIn('SDFORMAT', stage_bundle.APP_GROUPS['UTILS'])
        self.assertIn('appbuild SDFORMAT', (ROOT / 'scripts/build-bundle.sh').read_text())

    def test_storage_detach_and_drive_protection(self):
        import sys
        exe = Path(self.tmp.name) / 'sd-storage'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g',
            '-ffunction-sections', '-fdata-sections', '-fsanitize=address,undefined',
            '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections',
            '-DMACHINE_DREAMCAST', '-Iinclude', '-iquote', 'upstream/emutos/include',
            '-iquote', 'upstream/emutos/bdos', '-iquote', 'upstream/emutos/bios',
            'tests/sd_storage_host.c', 'src/dreamcast/sd_fat.c',
            'upstream/emutos/bdos/fsglob.c', 'upstream/emutos/bdos/fsdrive.c',
            '-o', str(exe)], cwd=ROOT, check=True)
        subprocess.run([str(exe)], check=True, timeout=10)

if __name__ == '__main__': unittest.main()
