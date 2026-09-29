"""Exercise real ported engines and file formats on the host under sanitizers."""
import os
import sys
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PUZZLE_COMMON = 'midend drawing misc malloc random tree234 dsf grid penrose penrose-legacy hat spectre sort tdq findloop'.split()

class BundleTests(unittest.TestCase):
    def build_run(self, name, source, extra=(), args=()):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)/name
            cmd = [os.environ.get('CC','cc'), '-std=gnu11', '-g', '-O1',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   '-ffunction-sections', '-fdata-sections',
                   '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',
                   '-D_GNU_SOURCE', '-DHAVE_STDINT_H', '-Iinclude', '-Iapps/lib',
                   '-Iapps/vendor/stb', '-Iapps/vendor/puzzles', source,
                   'tests/app_stubs.c', *extra, '-lm', '-o', str(out)]
            built=subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            # Some upstream engines intentionally cache immutable process-lifetime
            # geometry. The native OS reclaims all app allocations at Pterm.
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
            ran=subprocess.run([str(out), *map(str,args)],cwd=d,env=env,
                           capture_output=True,text=True,timeout=60)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

    def test_editor_edit_and_crlf_save_roundtrip(self):
        self.build_run('editor','tests/editor_host.c')

    def test_image_decode_quantize_transform_bmp_roundtrip(self):
        self.build_run('viewer','tests/viewer_host.c',args=[ROOT/'disc/SONIC.PNG',ROOT/'disc/SPACE.PNG'])

    def test_puzzle_engines_and_save_roundtrips(self):
        common=[f'apps/vendor/puzzles/{s}.c' for s in PUZZLE_COMMON]
        for game in ['fifteen','mines','net']:
            with self.subTest(game=game):
                self.build_run(game,'tests/puzzle_host.c',[*common,f'apps/vendor/puzzles/{game}.c'])

    def test_worm_movement_restart_and_high_scores(self):
        self.build_run('worm','tests/worm_host.c',[
            '-Iapps/vendor/gemworm','apps/vendor/gemworm/field.c',
            'apps/vendor/gemworm/player.c','apps/vendor/gemworm/scores.c'])

    def test_blocks_rules_kicks_scoring_and_scores(self):
        self.build_run('blocks','tests/blocks_host.c',['apps/ports/blocks_core.c'])

    def test_graphical_calculator_buttons_keyboard_and_errors(self):
        self.build_run('calc','tests/calc_host.c',[
            '-Iapps/vendor/tinyexpr','apps/vendor/tinyexpr/tinyexpr.c'])
