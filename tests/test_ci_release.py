"""Exercise release publication without contacting or modifying GitHub."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ('emutos-dreamcast.cdi', 'emutos-dreamcast.elf', 'BUILD-INFO.txt')


class ReleasePublication(unittest.TestCase):
    def run_release(self, existing=False, corrupt=False, missing=False):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / 'dist'
            output.mkdir()
            for name in ASSETS:
                (output / name).write_bytes(b'test artifact\n')
            (output / 'SHA256SUMS').write_text(''.join(
                f'{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}\n'
                for name in ASSETS
            ))
            if corrupt:
                (output / ASSETS[0]).write_bytes(b'changed after validation\n')
            if missing:
                (output / ASSETS[1]).unlink()
            gh = root / 'gh'
            gh.write_text('''#!/usr/bin/env python3
import json, os, pathlib, sys
args = sys.argv[1:]
with open(os.environ['GH_CALLS'], 'a') as log:
    log.write(json.dumps(args) + '\\n')
if args[:2] == ['release', 'view'] or (args[0] == 'api' and args[1] != '--method'):
    sys.exit(0 if os.environ['EXISTING_RELEASE'] == '1' else 1)
if '--notes-file' in args:
    notes = pathlib.Path(args[args.index('--notes-file') + 1]).read_text()
    assert 'abcdef0' in notes and 'development' in notes
if args[:2] in (['release', 'upload'], ['release', 'create']):
    for value in args[3:7]:
        assert pathlib.Path(value).is_file(), value
''')
            gh.chmod(0o755)
            # macOS has shasum; CI's Ubuntu publishing runner has sha256sum.
            checksum = root / 'sha256sum'
            checksum.write_text('#!/bin/sh\nexec shasum -a 256 "$@"\n')
            checksum.chmod(0o755)
            log = root / 'calls.jsonl'
            env = dict(os.environ, PATH=f'{root}:{os.environ["PATH"]}',
                       GH_CALLS=str(log), EXISTING_RELEASE=str(int(existing)),
                       GITHUB_REPOSITORY='example/emutos',
                       GITHUB_SHA='abcdef0123456789abcdef0123456789abcdef0123',
                       GH_TOKEN='test-only-placeholder')
            result = subprocess.run(
                ['bash', str(ROOT / '.github/scripts/publish-release.sh'), str(output)],
                env=env, capture_output=True, text=True)
            calls = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
            return result, calls

    def test_first_release_creates_tag_and_uploads_exact_assets(self):
        result, calls = self.run_release()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(any(call[:3] == ['api', '--method', 'POST'] for call in calls))
        self.assertTrue(any(call[:2] == ['release', 'create'] for call in calls))
        self.assertFalse(any(call[:2] == ['release', 'delete'] for call in calls))

    def test_existing_release_replaces_assets_and_updates_metadata(self):
        result, calls = self.run_release(existing=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(any(call[:3] == ['api', '--method', 'PATCH'] for call in calls))
        self.assertTrue(any(call[:2] == ['release', 'upload'] and '--clobber' in call for call in calls))
        self.assertTrue(any(call[:2] == ['release', 'edit'] for call in calls))
        self.assertFalse(any(call[:2] == ['release', 'delete'] for call in calls))

    def test_corrupt_artifacts_stop_before_github_mutation(self):
        result, calls = self.run_release(corrupt=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_missing_artifacts_stop_before_github_mutation(self):
        result, calls = self.run_release(missing=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])


if __name__ == '__main__':
    unittest.main()
