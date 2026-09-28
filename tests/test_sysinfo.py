"""Host integration check for the native system information utility."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SystemInfo(unittest.TestCase):
    def test_reports_queried_values_and_handles_old_api(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "sysinfo-test"
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                "-fsanitize=address,undefined", "-g", "-I", str(ROOT / "include"),
                str(ROOT / "tests/sysinfo_host.c"), "-o", str(output),
            ], check=True)
            subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    unittest.main()
