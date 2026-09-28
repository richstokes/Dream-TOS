"""Run the native glyph renderer on the host with bounds/UB instrumentation."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class Glyph(unittest.TestCase):
    def test_native_pixels(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = str(Path(temp) / 'glyph-test')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined', '-I'+str(ROOT/'include'),
                            str(ROOT/'tests/glyph_test.c'), str(ROOT/'src/dreamcast/glyph.c'),
                            '-o', binary], check=True)
            subprocess.run([binary], check=True)
