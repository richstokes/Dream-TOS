"""VMU Editor (VMUEDIT.PRG) on the host under ASan/UBSan.

* core: LCD bitmap operations, BMP import/export, VMS icon codec (CRC checked
  against the KOS routine), name helpers;
* GUI logic: the real apps/ports/vmuedit.c driven by synthetic mouse/keyboard
  events, talking to the real VMU file-service engine and a KOS-vmufs double
  over an in-memory card (import, export, rename, delete, protection,
  damaged and failing cards, LCD editing and live push, icon editing).
No real Dreamcast, VMU or display is involved."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-function', '-g',
         '-fsanitize=address,undefined', '-Iinclude', '-Iapps/lib', '-Iapps/ports', '-Itests/vmufs_shim']


class VmuEditor(unittest.TestCase):
    def build_run(self, sources):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / 'vmuedit-test'
            subprocess.run([os.environ.get('CC', 'cc'), *FLAGS, *sources, '-lm', '-o', str(binary)],
                           cwd=ROOT, check=True)
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', TMPDIR=temp)
            done = subprocess.run([str(binary)], cwd=temp, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
            self.assertIn('PASS', done.stdout)

    def test_core_lcd_bmp_vms_and_names(self):
        self.build_run(['tests/vmuedit_core_host.c', 'apps/ports/vmuedit_core.c'])

    def test_vms_built_by_the_fixture_generator_parses_in_c(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('vmu_fixture', ROOT / 'tools' / 'vmu_fixture.py')
        fixture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture)
        card = fixture.editor_card()
        self.assertEqual(card.fsck(), [])
        with tempfile.TemporaryDirectory() as temp:
            sample = Path(temp) / 'ICONTEST.VMS'
            sample.write_bytes(card.read(b'ICONTEST'))
            binary = Path(temp) / 'core-test'
            subprocess.run([os.environ.get('CC', 'cc'), *FLAGS, 'tests/vmuedit_core_host.c',
                            'apps/ports/vmuedit_core.c', '-o', str(binary)], cwd=ROOT, check=True)
            done = subprocess.run([str(binary), str(sample)], cwd=temp, capture_output=True, text=True)
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)

    def test_gui_against_file_service(self):
        self.build_run(['tests/vmuedit_host.c', 'tests/app_stubs.c', 'tests/vmufs_double.c',
                        'apps/ports/vmuedit_core.c', 'src/dreamcast/vmu_file.c', 'src/dreamcast/vmu_info.c'])


if __name__ == '__main__':
    unittest.main()
