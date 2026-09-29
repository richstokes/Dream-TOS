"""Network tools under ASan/UBSan against a scripted OS, plus packaging."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from stage_bundle import NET_TOOLS

class NetworkTools(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.bin = Path(cls.tmp.name)
        for tool in NET_TOOLS:
            name = tool.lower()
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Iinclude', '-Iapps/lib',
                f'-DNET_TOOL="{name}"', 'tests/net_tools_host.c', 'apps/ports/net_tools.c',
                '-o', str(cls.bin / name)], cwd=ROOT, check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_tool(self, tool, *args, code=0, **env):
        r = subprocess.run([str(self.bin/tool), *map(str, args)], capture_output=True, timeout=10,
                           env={**os.environ, **{k: str(v) for k, v in env.items()}})
        out = r.stdout.decode() + r.stderr.decode()
        self.assertEqual(r.returncode, code, out)
        self.assertNotIn('Sanitizer', out)
        return out

    def test_ifconfig_states(self):
        out = self.run_tool('ifconfig')
        for text in ('bba0: UP', '00:d0:f1:0a:0b:0c', 'MTU 1500', '192.168.1.50', '255.255.255.0',
                     'Gateway    192.168.1.1', 'DNS        192.168.1.1', '7 sent, 6 received, 1 send failures'):
            self.assertIn(text, out)
        self.assertIn('starting', self.run_tool('ifconfig', code=1, NET_STATE=0))
        self.assertIn('No network adapter', self.run_tool('ifconfig', code=1, NET_STATE=1))
        out = self.run_tool('ifconfig', code=1, NET_STATE=2)
        self.assertIn('DOWN', out); self.assertNotIn('Gateway', out)
        self.run_tool('ifconfig', 'x', code=2)

    def test_nslookup(self):
        out = self.run_tool('nslookup', 'example.test')
        self.assertIn('Server:  192.168.1.1', out)
        self.assertIn('Address: 93.184.216.34', out)
        self.assertIn('name not found', self.run_tool('nslookup', 'missing.test', code=1))
        self.assertIn('timed out', self.run_tool('nslookup', 'slow.test', code=1))
        self.assertIn('invalid host name', self.run_tool('nslookup', 'bad_name!', code=2))
        self.assertIn('reverse lookup', self.run_tool('nslookup', '1.2.3.4', code=2))
        self.run_tool('nslookup', code=2)
        self.run_tool('nslookup', 'example.test', code=1, NET_STATE=1)

    def test_ping_by_address_and_name(self):
        out = self.run_tool('ping', '-c', 2, '10.0.0.1')
        self.assertIn('PING 10.0.0.1 (10.0.0.1): 56 data bytes', out)
        self.assertIn('64 bytes from 10.0.0.1: icmp_seq=1 ttl=64 time=1.500 ms', out)
        self.assertIn('2 packets transmitted, 2 received, 0% packet loss', out)
        self.assertIn('min/avg/max = 1.500/2.000/2.500 ms', out)
        out = self.run_tool('ping', '-c', 1, '-s', 0, 'example.test')
        self.assertIn('(93.184.216.34): 0 data bytes', out)
        self.assertIn('8 bytes from', out)

    def test_ping_failures_and_arguments(self):
        out = self.run_tool('ping', '-c', 2, '10.0.0.9', code=1)
        self.assertIn('Request timeout for icmp_seq 1', out)
        self.assertIn('100% packet loss', out)
        self.assertIn('send failed', self.run_tool('ping', '-c', 1, '10.0.0.8', code=1))
        self.assertIn('network is down', self.run_tool('ping', '10.0.0.1', code=1, NET_STATE=1))
        self.assertIn('invalid IPv4', self.run_tool('ping', '300.1.1.1', code=2))
        self.assertIn('invalid IPv4', self.run_tool('ping', '01.2.3.4', code=2))
        self.assertIn('name not found', self.run_tool('ping', 'missing.test', code=1))
        for bad in (['-c', 0], ['-c', 1001], ['-s', 1401], ['-w', 99], ['-c'], ['-x']):
            self.run_tool('ping', *bad, '10.0.0.1', code=2)
        self.run_tool('ping', code=2)
        self.run_tool('ping', '10.0.0.1', '10.0.0.2', code=2)

    def test_ping_ctrl_c_summarizes(self):
        out = self.run_tool('ping', '-c', 5, '10.0.0.1', code=130, NET_BREAK=1)
        self.assertIn('packets transmitted', out)
        self.assertLess(out.count('icmp_seq='), 3)

    def test_old_os_api_is_reported(self):
        for tool in ('ping 10.0.0.1', 'nslookup example.test', 'ifconfig'):
            name, *args = tool.split()
            self.assertIn('not available', self.run_tool(name, *args, code=2, NET_OLD_API=1))

    def test_dns_wire_format_rejects_stale_replies(self):
        out = self.bin / 'dns'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-g', '-Wall', '-Wextra',
            '-fsanitize=address,undefined', '-Iinclude', 'tests/dns_host.c', 'src/dreamcast/dns.c',
            '-o', str(out)], cwd=ROOT, check=True)
        r = subprocess.run([str(out)], capture_output=True, timeout=10)
        self.assertEqual(r.returncode, 0, r.stderr.decode())
        self.assertIn(b'dns ok', r.stdout)

    def test_help(self):
        for tool in NET_TOOLS:
            self.assertIn(tool.lower(), self.run_tool(tool.lower(), '--help'))

if __name__ == '__main__':
    unittest.main()
