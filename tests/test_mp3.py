"""Host tests for the audio ABI shim/ring buffer and the MP3 player (decoder, tags, UI)."""
import os
import sys
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CC = os.environ.get('CC', 'cc')


def build(out, sources, extra=()):
    cmd = [CC, '-std=gnu11', '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
           '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-missing-field-initializers',
           '-D_GNU_SOURCE', '-Iinclude', '-Iapps/lib', '-Iapps/vendor/minimp3', '-Iapps/ports',
           *sources, *extra, '-lm', '-o', str(out)]
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    assert done.returncode == 0, done.stderr
    return done.stderr


class Audio(unittest.TestCase):
    def test_abi_shim_and_ring_buffer(self):
        with tempfile.TemporaryDirectory() as d:
            exe = Path(d) / 'audio'
            build(exe, ['tests/audio_host.c'])
            ran = subprocess.run([str(exe)], cwd=d, capture_output=True, text=True, timeout=60)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_native_api_exposes_audio_block_and_loader_stops_stream(self):
        header = (ROOT / 'include/dreamcast/native.h').read_text()
        native = (ROOT / 'src/dreamcast/native.c').read_text()
        for name in ('audio_open', 'audio_close', 'audio_write', 'audio_space', 'audio_set', 'audio_info'):
            self.assertIn(f'(*{name})', header)
        # Appended after the previous optional members, so their offsets are unchanged.
        self.assertGreater(header.index('audio_open'), header.index('net_resolve'))
        # The stream is stopped whenever a program returns or terminates.
        self.assertRegex(native, r'dc_audio_close\(\);[^\n]*\n\s*dc_free_process_memory')


class Mp3Player(unittest.TestCase):
    def test_tags_playlist_decoder_ui_and_audio_pipeline(self):
        with tempfile.TemporaryDirectory() as d:
            exe = Path(d) / 'mp3'
            build(exe, ['tests/mp3_host.c', 'tests/app_stubs.c', 'apps/ports/mp3_core.c'])
            scratch = Path(d) / 'files'
            scratch.mkdir()
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
            ran = subprocess.run([str(exe), str(scratch)], cwd=d, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_minimp3_is_vendored_with_licence_and_notice(self):
        self.assertTrue((ROOT / 'apps/vendor/minimp3/minimp3.h').is_file())
        self.assertIn('CC0 1.0 Universal', (ROOT / 'apps/vendor/minimp3/LICENSE').read_text())
        import importlib.util
        spec = importlib.util.spec_from_file_location('stage_bundle', ROOT / 'tools/stage_bundle.py')
        stage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(stage)
        self.assertIn('MP3', stage.APPS)
        self.assertEqual(stage.NOTICES['MP3LIC.TXT'], 'apps/vendor/minimp3/LICENSE')
        self.assertIn('minimp3', (ROOT / 'apps/vendor/sources.json').read_text())

    @unittest.skipUnless((ROOT / 'build/apps/MP3.PRG').is_file(), 'build the bundle first')
    def test_native_program_is_a_valid_image(self):
        self.assertEqual((ROOT / 'build/apps/MP3.PRG').read_bytes()[:8], b'DCNATIVE')


if __name__ == '__main__':
    unittest.main()
