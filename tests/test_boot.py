"""Exercise the direct boot/crash renderer with the real built-in font."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BootDisplay(unittest.TestCase):
    def test_font_and_framebuffer_bounds(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "boot-display"
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined",
                "-DMACHINE_DREAMCAST", "-Iinclude",
                "-iquote", "upstream/emutos/include",
                "-iquote", "upstream/emutos/bios",
                "tests/boot_host.c", "src/dreamcast/boot.c",
                "upstream/emutos/bios/fnt_st_8x16.c",
                "upstream/emutos/bios/fnt_off_8x8.c", "-o", str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)
