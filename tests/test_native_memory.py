"""Check the real native memory-owner selection and GEMDOS allocation arena."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeMemory(unittest.TestCase):
    def test_accessory_memory_survives_foreground_exit(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "native-memory"
            includes = ["-Iinclude"]
            for folder in ("include", "aes", "bdos", "bios", "vdi"):
                includes += ["-iquote", f"upstream/emutos/{folder}"]
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=gnu11", "-g", "-O1",
                "-fsanitize=address,undefined", "-ffunction-sections", "-fdata-sections",
                "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                "-DMACHINE_DREAMCAST", *includes, "tests/native_memory_host.c",
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)
