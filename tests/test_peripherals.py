"""Native settings and read-only VMU parser with malformed-card fixtures."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class Peripherals(unittest.TestCase):
    def test_settings_and_readonly_card_parser(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/'peripherals'
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra',
                '-fsanitize=address,undefined','-g','-Iinclude','tests/peripherals_host.c',
                'src/dreamcast/input_config.c','src/dreamcast/vmu_info.c','-o',str(binary)],cwd=ROOT,check=True)
            subprocess.run([str(binary)],check=True)
