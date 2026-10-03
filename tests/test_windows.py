"""Exercise real native AES/VDI bindings and window lifecycle under sanitizers."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeWindows(unittest.TestCase):
    def build_run(self, source, defines=()):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "window-test"
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                "-fsanitize=address,undefined", "-g", "-Iinclude", "-Iapps/lib",
                source, "apps/lib/app.c", "apps/lib/window.c", "apps/lib/accessory.c", "-lm", *defines,
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)

    def test_geometry_clipped_redraw_events_and_lifecycle(self):
        self.build_run("tests/window_host.c")

    def test_editor_menus_scrollbars_and_incremental_redraw(self):
        self.build_run("tests/editor_gem_host.c", [
            "-DAPP_HOST_TEST", "apps/ports/editor_core.c", "apps/ports/editor_file.c"])

    def test_clock_accessory_lifecycle_and_midnight(self):
        self.build_run("tests/clock_host.c")

    def test_calculator_accessory_lifecycle_and_state(self):
        self.build_run("tests/calc_accessory_host.c", [
            "-Iapps/vendor/tinyexpr", "apps/vendor/tinyexpr/tinyexpr.c"])

    def test_resident_utility_window_lifecycle(self):
        self.build_run("tests/accessory_host.c")

    def test_control_monitor_and_vmu_frontends(self):
        for define in ("TEST_CONTROL", "TEST_MONITOR", "TEST_VMUTOOL"):
            with self.subTest(utility=define):
                self.build_run("tests/utility_host.c", ["-D"+define])
