"""Verify benchmark accounting, kernels and non-destructive file handling."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Benchmark(unittest.TestCase):
    def test_measurements_and_file_lifecycle(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'bench-test'
            subprocess.run([
                os.environ.get('CC', 'cc'), '-std=gnu11', '-O2', '-g', '-ffp-contract=off',
                '-Wall', '-Wextra', '-Wno-missing-field-initializers',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-Iinclude', '-Iapps/lib', 'tests/bench_host.c',
                'tests/app_stubs.c', 'apps/ports/bench_core.c', '-o', str(output),
            ], cwd=ROOT, check=True)
            subprocess.run([str(output)], cwd=temp, check=True, timeout=30)

    def test_benchmark_is_staged_with_deterministic_input(self):
        spec = importlib.util.spec_from_file_location('stage_bundle', ROOT/'tools/stage_bundle.py')
        stage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(stage)
        with tempfile.TemporaryDirectory() as temp:
            dest = Path(temp)
            stage.stage(ROOT, dest)
            self.assertEqual((dest/'BENCH.PRG').read_bytes()[:8], b'DCNATIVE')
            self.assertEqual((dest/'BENCH.DAT').read_bytes(), bytes(range(256))*1024)
