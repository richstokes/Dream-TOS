"""Exercise real native AES/VDI bindings and window lifecycle under sanitizers."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeWindows(unittest.TestCase):
    def test_geometry_clipped_redraw_events_and_lifecycle(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "window-test"
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                "-fsanitize=address,undefined", "-g", "-Iinclude", "-Iapps/lib",
                "tests/window_host.c", "apps/lib/app.c", "apps/lib/window.c",
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)
