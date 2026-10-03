"""Real passive FTP sessions under ASan/UBSan, including fragmented/short I/O."""
import ftplib
import io
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.build.name) / 'ftp'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                        '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-Iinclude', '-Iapps/lib', '-Iapps/ports', 'tests/ftp_host.c',
                        'apps/ports/ftp_core.c', 'apps/ports/ftp_fs.c', 'src/dreamcast/hal_tcp.c',
                        '-o', str(cls.binary)], cwd=ROOT, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for drive in 'CDE':
            (self.root / drive / 'SHARE' / 'SUB').mkdir(parents=True)
            (self.root / drive / 'SHARE' / 'HELLO.TXT').write_bytes(b'Hello\r\n')
            (self.root / drive / 'SECRET.TXT').write_bytes(b'outside')
        self.process = None
        self.ftp = None

    def tearDown(self):
        if self.ftp:
            self.ftp.close()
        if self.process:
            if self.process.poll() is None:
                self.process.stdin.write(b'S')
                self.process.stdin.flush()
            out, err = self.process.communicate(timeout=5)
            self.assertEqual(self.process.returncode, 0, (out + err).decode())
            self.assertNotIn(b'Sanitizer', err)
        self.temp.cleanup()

    def start(self, share='C:\\SHARE', writable=True, login=True):
        self.process = subprocess.Popen([str(self.binary), share, str(int(writable))], cwd=self.root,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        port = int(self.process.stdout.readline())
        self.ftp = ftplib.FTP(timeout=5)
        self.ftp.connect('127.0.0.1', port)
        if login:
            self.assertTrue(self.ftp.login().startswith('230'))
        return self.ftp

    def inject(self, command):
        self.process.stdin.write(command.encode())
        self.process.stdin.flush()
        time.sleep(.03)

    def test_path_rules_and_socket_handle_cleanup(self):
        run = subprocess.run([str(self.binary), '--selftest'], capture_output=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stderr.decode())

    def test_no_auth_list_cwd_download_and_upload(self):
        ftp = self.start(login=False)
        self.assertEqual(ftp.pwd(), '/')
        self.assertEqual(set(ftp.nlst()), {'HELLO.TXT', 'SUB'})
        lines = []
        ftp.retrlines('LIST', lines.append)
        self.assertTrue(any(line.startswith('d') and line.endswith('SUB') for line in lines))
        self.assertEqual(dict(ftp.mlsd())['HELLO.TXT']['size'], '7')
        ftp.cwd('sub')
        self.assertEqual(ftp.pwd(), '/SUB')
        payload = bytes(range(256)) * 700
        ftp.storbinary('STOR big.bin', io.BytesIO(payload))
        self.assertEqual(ftp.size('big.bin'), len(payload))
        read = bytearray()
        ftp.retrbinary('RETR BIG.BIN', read.extend)
        self.assertEqual(read, payload)
        self.assertEqual((self.root / 'C/SHARE/SUB/BIG.BIN').read_bytes(), payload)
        ftp.cwd('..')
        ftp.cwd('..')
        self.assertEqual(ftp.pwd(), '/')
        ftp.quit()

    def test_epsv_and_empty_files(self):
        ftp = self.start()
        ftp.voidcmd('TYPE I')
        port = ftplib.parse229(ftp.sendcmd('EPSV'), ftp.sock.getpeername())[1]
        with socket.create_connection(('127.0.0.1', port), timeout=3) as data:
            self.assertTrue(ftp.sendcmd('STOR ZERO.BIN').startswith('150'))
        self.assertTrue(ftp.voidresp().startswith('226'))
        result = bytearray()
        ftp.retrbinary('RETR ZERO.BIN', result.extend)
        self.assertEqual(result, b'')
        self.assertEqual(list(ftp.mlsd('SUB')), [])

    def test_sd_root_subtree_and_file_operations(self):
        ftp = self.start('E:\\SHARE')
        self.assertEqual(ftp.mkd('NEW'), '/NEW')
        ftp.storbinary('STOR NEW/A.TXT', io.BytesIO(b'test'))
        ftp.rename('NEW/A.TXT', 'NEW/B.TXT')
        ftp.delete('NEW/B.TXT')
        ftp.rmd('NEW')
        ftp.storbinary('STOR HELLO.TXT', io.BytesIO(b'replaced'))
        self.assertEqual((self.root / 'E/SHARE/HELLO.TXT').read_bytes(), b'replaced')
        self.assertEqual((self.root / 'E/SECRET.TXT').read_bytes(), b'outside')

    def test_readonly_disc(self):
        ftp = self.start('D:\\SHARE', writable=False)
        for command in ['STOR A.TXT', 'DELE HELLO.TXT', 'MKD NEW', 'RMD SUB', 'RNFR HELLO.TXT']:
            ftp.voidcmd('TYPE I')
            with self.assertRaises(ftplib.error_perm):
                ftp.sendcmd(command)
        result = bytearray()
        ftp.retrbinary('RETR HELLO.TXT', result.extend)
        self.assertEqual(result, b'Hello\r\n')

    def test_paths_cannot_escape_or_alias_dos_names(self):
        ftp = self.start()
        for path in ['../SECRET.TXT', '/../../SECRET.TXT', 'C:/SECRET.TXT', '..\\SECRET.TXT',
                     'C:SECRET.TXT', 'A*', 'A?', 'TOOLONGGG.TXT', 'HELLO.TXT.', 'A B', 'A\rB']:
            with self.subTest(path=path):
                with self.assertRaises(ftplib.error_perm):
                    # Raw CR is deliberately tested below, since ftplib forbids it.
                    ftp.size(path) if '\r' not in path else ftp.size('A;B')
        for command in ['DELE /', 'RMD /', 'RNFR /', 'CWD /MISSING', 'PORT 127,0,0,1,1,1']:
            with self.assertRaises(ftplib.error_perm):
                ftp.sendcmd(command)
        ftp.cwd('SUB')
        ftp.cwd('/SUB/..')
        self.assertEqual(ftp.pwd(), '/')
        self.assertEqual((self.root / 'C/SECRET.TXT').read_bytes(), b'outside')

    def test_fragmented_pipelined_and_malformed_commands(self):
        ftp = self.start()
        for piece in [b'PW', b'D\r', b'\nNOOP\r\n']:
            ftp.sock.sendall(piece)
        self.assertTrue(ftp.getresp().startswith('257'))
        self.assertTrue(ftp.getresp().startswith('200'))
        for command in [b'X' * 1500 + b'\r\n', b'DELE HELLO.TXT\x00IGNORED\r\n', b'DELE HELLO.TXT\rEXTRA\r\n']:
            ftp.sock.sendall(command)
            with self.assertRaises(ftplib.error_perm):
                ftp.getresp()
            self.assertTrue(ftp.voidcmd('NOOP').startswith('200'))
        self.assertTrue((self.root / 'C/SHARE/HELLO.TXT').exists())

    def test_missing_data_timeout_does_not_truncate_upload(self):
        ftp = self.start()
        ftp.voidcmd('TYPE I')
        ftp.sendcmd('PASV')
        self.assertTrue(ftp.sendcmd('STOR HELLO.TXT').startswith('150'))
        self.inject('T')
        with self.assertRaises(ftplib.error_temp):
            ftp.getresp()
        self.assertEqual((self.root / 'C/SHARE/HELLO.TXT').read_bytes(), b'Hello\r\n')
        self.assertTrue(ftp.voidcmd('NOOP').startswith('200'))

    def test_abort_transfer_and_client_reconnect(self):
        ftp = self.start()
        ftp.voidcmd('TYPE I')
        ftp.sendcmd('PASV')
        ftp.sendcmd('STOR A.BIN')
        ftp.putcmd('ABOR')
        with self.assertRaises(ftplib.error_temp):
            ftp.getresp()
        self.assertTrue(ftp.getresp().startswith('226'))
        port = ftp.sock.getpeername()[1]
        ftp.close()
        self.ftp = ftplib.FTP(timeout=5)
        self.ftp.connect('127.0.0.1', port)
        self.assertEqual(self.ftp.pwd(), '/')

    def test_write_read_and_close_errors_report_failure(self):
        ftp = self.start()
        self.inject('W')
        with self.assertRaises(ftplib.error_temp):
            ftp.storbinary('STOR FAIL.BIN', io.BytesIO(b'payload'))
        self.inject('R')
        with self.assertRaises(ftplib.error_temp):
            ftp.retrbinary('RETR HELLO.TXT', lambda b: None)
        self.inject('C')
        with self.assertRaises(ftplib.error_temp):
            ftp.storbinary('STOR EMPTY.BIN', io.BytesIO())

    def test_idle_timeout(self):
        ftp = self.start()
        self.inject('I')
        with self.assertRaises((EOFError, ConnectionError, OSError)):
            ftp.voidcmd('NOOP')

    def test_foreign_passive_peer_is_rejected(self):
        ftp = self.start()
        host, port = ftplib.parse227(ftp.sendcmd('PASV'))
        foreign = socket.socket()
        try:
            self.inject('P')
            foreign.settimeout(3)
            foreign.connect((host, port))
            self.assertEqual(foreign.recv(1), b'')
        finally:
            foreign.close()
        with socket.create_connection((host, port), timeout=3) as data:
            self.assertTrue(ftp.sendcmd('NLST').startswith('150'))
            listing = bytearray()
            while chunk := data.recv(4096):
                listing.extend(chunk)
        self.assertIn(b'HELLO.TXT', listing)
        self.assertTrue(ftp.voidresp().startswith('226'))

    def test_long_active_transfer_does_not_expire_control_session(self):
        ftp = self.start()
        ftp.voidcmd('TYPE I')
        with ftp.transfercmd('STOR LONG.BIN') as data:
            # Simulate a slow SD transfer lasting over five minutes, while
            # continuing to make progress inside the 30-second data timeout.
            for _ in range(16):
                self.inject('L')
                data.sendall(b'chunk')
                time.sleep(.01)
        self.assertTrue(ftp.voidresp().startswith('226'))
        self.assertEqual(ftp.size('LONG.BIN'), 80)

    def test_stop_closes_active_upload_and_all_sockets(self):
        ftp = self.start()
        ftp.voidcmd('TYPE I')
        data = ftp.transfercmd('STOR PART.BIN')
        try:
            data.sendall(b'partial')
            time.sleep(.03)
            self.inject('S')
            self.process.wait(timeout=3)
            self.assertEqual(self.process.returncode, 0)
            self.assertEqual(data.recv(1), b'')
            with self.assertRaises((EOFError, ConnectionError, OSError)):
                ftp.voidcmd('NOOP')
        finally:
            data.close()

    def test_graphical_picker_and_lifecycle(self):
        binary = Path(self.build.name) / 'ftp-ui'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                        '-fsanitize=address,undefined', '-Iinclude', '-Iapps/lib', '-Iapps/ports',
                        'tests/ftp_ui_host.c', 'apps/ports/ftp_core.c', 'apps/lib/app.c',
                        'apps/lib/window.c', '-o', str(binary)], cwd=ROOT, check=True)
        run = subprocess.run([str(binary)], capture_output=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stderr.decode())

    def test_packaging(self):
        import sys
        sys.path.insert(0, str(ROOT / 'tools'))
        import stage_bundle
        self.assertIn('FTP', stage_bundle.APPS)
        self.assertIn('appbuild FTP', (ROOT / 'scripts/build-bundle.sh').read_text())


if __name__ == '__main__':
    unittest.main()
