"""Real CLI filters under ASan/UBSan, plus packaged native executable checks."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from stage_bundle import CLI_TOOLS
import native_app

class ConsoleTools(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.bin = Path(cls.tmp.name)
        for tool in CLI_TOOLS:
            name = tool.lower()
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g',
                '-Wall', '-Wextra', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-Iinclude', '-Iapps/lib', '-Iapps/vendor/tinyexpr', f'-DCLI_TOOL="{name}"',
                'tests/cli_tools_host.c', 'apps/ports/cli_tools.c', 'apps/vendor/tinyexpr/tinyexpr.c',
                '-lm', '-o', str(cls.bin / name)], cwd=ROOT, check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_tool(self, tool, *args, code=0, data=None):
        result = subprocess.run([str(self.bin/tool), *map(str, args)], input=data,
            capture_output=True, timeout=10, cwd=self.bin)
        self.assertEqual(result.returncode, code, result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
        self.assertNotIn(b'Sanitizer', result.stderr)
        return result.stdout.decode()

    def fixture(self, name, content):
        (self.bin/name).write_bytes(content)
        return name

    def test_counts_binary_and_crlf(self):
        f = self.fixture('words.txt', b'alpha beta\r\ngamma\nlast')
        self.assertEqual(self.run_tool('wc', f), f'2 4 22 {f}\n')
        self.assertEqual(self.run_tool('wc', '-lc', f), f'2 22 {f}\n')
        self.assertEqual(self.run_tool('wc', '-w', '-c', '-l', f), f'2 4 22 {f}\n')
        self.assertEqual(self.run_tool('wc', f, f).splitlines()[-1], '4 8 44 total')
        self.run_tool('wc', 'missing', code=2)

    def test_literal_grep_and_long_lines(self):
        f = self.fixture('grep.txt', b'Alpha\r\na.b\naxb\nfinal')
        self.assertEqual(self.run_tool('grep', '-in', 'alpha', f), '1:Alpha\n')
        self.assertEqual(self.run_tool('grep', 'a.b', f), 'a.b\n')
        self.assertEqual(self.run_tool('grep', '-v', 'a.b', f), 'Alpha\naxb\nfinal\n')
        self.run_tool('grep', 'absent', f, code=1)
        self.run_tool('grep', '[', f, code=1)
        self.assertEqual(self.run_tool('grep', '', f), 'Alpha\na.b\naxb\nfinal\n')
        long = self.fixture('long.txt', b'x'*10000+b'end')
        self.assertEqual(len(self.run_tool('grep', 'end', long)), 10004)
        overflow = self.fixture('overflow.txt', b'x'*65537)
        self.run_tool('grep', 'x', overflow, code=2)
        binary = self.fixture('binary.dat', b'a\0b')
        self.run_tool('grep', 'a', binary, code=2)

    def test_head_tail_boundaries(self):
        f = self.fixture('lines.txt', b'one\r\ntwo\nthree\nfour')
        self.assertEqual(self.run_tool('head', '-n', 2, f), 'one\ntwo\n')
        self.assertEqual(self.run_tool('tail', '-n', 2, f), 'three\nfour\n')
        for tool in ('head', 'tail'):
            self.assertEqual(self.run_tool(tool, '-n', 0, f), '')
            self.assertEqual(self.run_tool(tool, f), 'one\ntwo\nthree\nfour\n')
            self.run_tool(tool, '-n', '-1', f, code=2)
            self.run_tool(tool, '-n', 'bogus', f, code=2)
            self.run_tool(tool, '-n', 10001, f, code=2)
        self.assertEqual(self.run_tool('tail', '-n', 1, '-', data=b'a\nb\n'), 'b\n')
        empty = self.fixture('empty', b'')
        self.assertEqual(self.run_tool('tail', empty), '')

    def test_sort_multiple_files_and_unique_reverse(self):
        f = self.fixture('sort.txt', b'z\r\na\na\nb')
        g = self.fixture('sort2.txt', b'c\na\n')
        self.assertEqual(self.run_tool('sort', f), 'a\na\nb\nz\n')
        self.assertEqual(self.run_tool('sort', '-r', '-u', f, g), 'z\nc\nb\na\n')
        f = self.fixture('many.txt', b'x\n'*10001)
        self.run_tool('sort', f, code=2)
        f = self.fixture('large.txt', (b'x'*1000+b'\n')*1100)
        self.run_tool('sort', f, code=2)

    def test_crc_matches_host_posix_cksum(self):
        for data in (b'', b'123456789', bytes(range(256))*32+b'last'):
            f = self.fixture('crc.bin', data)
            expected = subprocess.check_output(['cksum', f], cwd=self.bin).decode()
            self.assertEqual(self.run_tool('cksum', f), expected)

    def test_hex_and_system_tools(self):
        f = self.fixture('hex.bin', b'A\x00\xff\n')
        out = self.run_tool('hexdump', f)
        self.assertIn('00000000  41 00 ff 0a', out)
        self.assertIn('|A...|', out)
        self.assertEqual(self.run_tool('date'), '2026-09-29 13:42:30\n')
        self.assertIn('SH-4', self.run_tool('uname'))
        self.assertIn('640x480', self.run_tool('uname', '-a'))
        self.assertIn('GEMDOS free: 3000000', self.run_tool('free'))
        self.assertIn('read-only', self.run_tool('df', 'D:'))
        self.assertNotIn('RAM', self.run_tool('df', 'D:'))
        self.run_tool('df', 'A:', code=2)
        self.run_tool('date', 'unexpected', code=2)
        self.assertEqual(self.run_tool('expr', 'sqrt(144)+2^3'), '20\n')
        self.run_tool('expr', '1/0', code=2)
        self.run_tool('expr', 'bad(', code=2)

    def test_help_and_argument_errors(self):
        for tool in CLI_TOOLS:
            name = tool.lower()
            self.assertIn(name, self.run_tool(name, '--help'))
        for name in ('grep', 'wc', 'head', 'tail', 'sort', 'cksum', 'hexdump', 'expr'):
            self.run_tool(name, code=2)

    def test_native_tools_are_packaged(self):
        image = (ROOT/'build/disc/DISC.IMG').read_bytes()
        entries = [image[i:i+32] for i in range(65*512, 73*512, 32)]
        import struct
        for name in CLI_TOOLS:
            expected = native_app.convert((ROOT/f'build/apps/{name}.elf').read_bytes())
            self.assertEqual((ROOT/f'build/apps/{name}.TTP').read_bytes(), expected)
            entry = next(e for e in entries if e[:11] == f'{name:<8}TTP'.encode())
            cluster = struct.unpack_from('<H', entry, 26)[0]
            size = struct.unpack_from('<I', entry, 28)[0]
            payload = bytearray()
            seen = set()
            while cluster < 0xfff8:
                self.assertGreaterEqual(cluster, 2)
                self.assertNotIn(cluster, seen)
                seen.add(cluster)
                pos = (73 + cluster - 2)*512
                payload += image[pos:pos+512]
                cluster = struct.unpack_from('<H', image, 512 + cluster*2)[0]
            self.assertEqual(payload[:size], expected)

class ConsoleParser(unittest.TestCase):
    def test_parser_and_command_tail_bounds(self):
        for source in ('cli_parse_host.c', 'cli_tail_host.c', 'cli_entry_host.c'):
            with self.subTest(source=source), tempfile.TemporaryDirectory() as d:
                out = Path(d)/'test'
                subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g',
                    '-fsanitize=address,undefined', '-ffunction-sections', '-fdata-sections',
                    '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections',
                    '-Iupstream/emutos/include', f'tests/{source}', '-o', str(out)],
                    cwd=ROOT, check=True, capture_output=True)
                subprocess.run([str(out)], check=True)
