"""The SD boot probe must not leave logging on the serial pins during SPI."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SdHal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / 'sd_hal'
        subprocess.run([
            os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-g', '-Itests/sd_kos', '-Iinclude',
            'tests/sd_hal_host.c', 'src/dreamcast/hal_sd.c', '-o', str(cls.exe),
        ], cwd=ROOT, check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def check_probe(self, scenario):
        result = subprocess.run([str(self.exe), scenario], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout.decode() + result.stderr.decode())

    def test_no_card_restores_console(self):
        self.check_probe('no-card')

    def test_failed_probe_preserves_disabled_console(self):
        self.check_probe('disabled')

    def test_size_failure_restores_console_after_shutdown(self):
        self.check_probe('size-failure')

    def test_success_keeps_serial_console_disabled(self):
        self.check_probe('success')

    def test_failed_probe_can_be_retried(self):
        self.check_probe('retry')

    def test_console_suppression_failure_skips_probe(self):
        self.check_probe('no-null-handler')


if __name__ == '__main__':
    unittest.main()
