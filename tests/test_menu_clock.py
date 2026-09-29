"""Check the native menu clock's lifecycle and protected drawing region."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MenuClock(unittest.TestCase):
    def test_clock_and_menu_lifecycle(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "menu-clock"
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-DMACHINE_DREAMCAST",
                "-Iinclude", "-Iupstream/emutos/include", "-Iupstream/emutos/aes",
                "-Iupstream/emutos/bdos", "tests/menu_clock_host.c",
                "src/dreamcast/menu_clock.c", "-o", str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)
