"""Live protocol tests. Run using scripts/test-ssh.sh (Paramiko + pexpect)."""
import contextlib
import hashlib
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import paramiko
import pexpect
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from prepare_ssh import create_seed
BINARY = ROOT/'build/ssh-host'

class Server(paramiko.ServerInterface):
    def __init__(self, mode, key=None):
        self.mode, self.key = mode, key
        self.auth = []
        self.pty = None
        self.shell = threading.Event()
    def get_allowed_auths(self, user):
        return {'password':'password','interactive':'keyboard-interactive',
                'key':'publickey','multi':'publickey,keyboard-interactive'}[self.mode]
    def check_auth_password(self, user, password):
        self.auth.append((user,password))
        return paramiko.AUTH_SUCCESSFUL if (user,password)==('tester','correct horse') else paramiko.AUTH_FAILED
    def check_auth_publickey(self, user, key):
        self.auth.append((user,key.get_name()))
        if self.key and key.asbytes()==self.key.asbytes():
            return paramiko.AUTH_PARTIALLY_SUCCESSFUL if self.mode=='multi' else paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED
    def check_auth_interactive(self, user, submethods):
        self.auth.append((user,'interactive'))
        return paramiko.InteractiveQuery('Two step login','Enter both values',('Password: ',False),('OTP: ',True))
    def check_auth_interactive_response(self, responses):
        self.auth.append(responses)
        return paramiko.AUTH_SUCCESSFUL if responses==['correct horse','123456'] else paramiko.AUTH_FAILED
    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind=='session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
    def check_channel_pty_request(self, chanid, term, width, height, pw, ph, modes):
        self.pty=(term,width,height)
        return True
    def check_channel_shell_request(self, channel):
        self.shell.set()
        return True

class LiveSSH(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.hostkey=paramiko.RSAKey.generate(2048)
        cls.temp=tempfile.TemporaryDirectory(prefix='dc-ssh-')
        cls.root=Path(cls.temp.name)
        cls.keys={}
        for kind in ('ed25519','rsa','ecdsa'):
            path=cls.root/kind
            subprocess.run(['ssh-keygen','-q','-t',kind,'-N','key secret','-f',str(path)],check=True)
            cls.keys[kind]=path
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(dir=self.root)
        self.folder=Path(self.tmp.name)
        self.seed=create_seed(self.folder)
        self.hosts=self.folder/'HOSTS.TXT'
    def tearDown(self):self.tmp.cleanup()
    def run_session(self, mode='password', identity=None, cipher=None, rekey=False, partial=False, trusted=False, reject=False, seed_state="secure", hosts_missing=False):
        key=paramiko.PKey.from_path(identity,password=b'key secret') if identity else None
        server=Server('multi' if partial else mode,key)
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(15)
        port=listener.getsockname()[1];errors=[];received=[]
        def serve():
            try:
                sock,_=listener.accept()
                with contextlib.closing(paramiko.Transport(sock)) as transport:
                    transport.add_server_key(self.hostkey)
                    if cipher:transport.get_security_options().ciphers=[cipher]
                    transport.start_server(server=server)
                    ch=transport.accept(15)
                    if reject:
                        assert not ch and not server.auth,server.auth
                        return
                    assert ch and server.shell.wait(15),server.auth
                    ch.settimeout(15)
                    if rekey:transport.renegotiate_keys()
                    ch.sendall(b'\x1b[2J\x1b[HREADY\r\n')
                    data=b''
                    while not data.endswith(b'\r'):data+=ch.recv(1024)
                    received.append(data)
                    # Exercise receive window updates and scrolling beyond one window.
                    ch.sendall(b'line of output\r\n'*4000+b'FINISHED\r\n')
                    ch.send_exit_status(7);ch.shutdown_write();time.sleep(.2);ch.close();time.sleep(.2)
            except BaseException as e:errors.append(e)
            finally:listener.close()
        thread=threading.Thread(target=serve,daemon=True);thread.start()
        args=['-s',str(self.seed),'-K',str(self.hosts),'-p',str(port)]
        if identity:args+=['-i',str(identity)]
        args+=['tester@127.0.0.1']
        if trusted or reject:
            import base64
            digest=hashlib.sha256(self.hostkey.asbytes()).digest() if trusted else bytes(32)
            self.hosts.write_text(f'127.0.0.1 {port} SHA256:'+base64.b64encode(digest).decode().rstrip('=')+'\n')
        if seed_state=='missing':self.seed.unlink()
        elif seed_state=='corrupt':self.seed.write_bytes(b'damaged seed')
        if hosts_missing:self.hosts=self.folder/'NEW'/'HOSTS.TXT'
        # Rebuild the hosts argument after choosing an initially missing folder.
        args[args.index('-K')+1]=str(self.hosts)
        before=self.seed.read_bytes() if self.seed.exists() else None
        child=pexpect.spawn(str(BINARY),args,encoding='utf8',codec_errors='replace',timeout=20)
        log=[];child.logfile_read=type('Log',(),{'write':lambda _,s:log.append(s),'flush':lambda _:None})()
        try:
            if seed_state!='secure':
                child.expect_exact('Type risk to continue for this connection (Enter cancels): ')
                self.assertIn('weak clock/timing randomness',''.join(log))
                self.assertIn('private keys',''.join(log))
                child.send('risk\r')
            if identity:child.expect_exact('Key passphrase: ');child.send('key secret\r')
            if reject:
                child.expect_exact('HOST KEY CHANGED');child.expect(pexpect.EOF);child.close()
                self.assertEqual(child.exitstatus,1);self.assertFalse(server.auth)
                return
            if not trusted:child.expect_exact('Type yes: ');child.send('yes\r')
            if mode=='password':child.expect_exact('Password: ');child.send('correct horse\r')
            if mode=='interactive' or partial:
                child.expect_exact('Password: ');child.send('correct horse\r')
                child.expect_exact('OTP: ');child.send('123456\r')
            child.expect_exact('READY');child.send('hello\x03world\r')
            child.expect_exact('Connection closed.');child.expect(pexpect.EOF);child.close()
            self.assertEqual(child.exitstatus,7, ''.join(log))
            if seed_state=='secure':
                self.assertNotEqual(before,self.seed.read_bytes())
                self.assertNotIn('Type risk',''.join(log))
            elif before is None:self.assertFalse(self.seed.exists())
            else:self.assertEqual(before,self.seed.read_bytes())
            self.assertIn('SHA256:',self.hosts.read_text())
            self.assertNotIn('correct horse',''.join(log))
            self.assertIn('FINISHED',''.join(log))
            self.assertEqual(server.pty,(b'xterm',80,30))
            self.assertEqual(received,[b'hello\x03world\r'])
        except BaseException:
            print('CLIENT OUTPUT:', ''.join(log),file=sys.stderr)
            raise
        finally:
            child.close(force=True);thread.join(3)
        self.assertFalse(errors,errors)
    def test_missing_seed_accepts_risk_and_makes_host_folder(self):
        self.run_session(seed_state='missing',hosts_missing=True)
    def test_corrupt_seed_accepts_risk_without_replacing_it(self):
        self.run_session(seed_state='corrupt')
    def test_weak_randomness_with_encrypted_identity(self):
        self.run_session('key',self.keys['ed25519'],seed_state='missing')
    def test_weak_randomness_still_rejects_changed_host(self):
        self.run_session(seed_state='missing',reject=True)
    def test_declined_risk_never_connects(self):
        self.seed.unlink()
        with socket.socket() as listener:
            listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(.1)
            for answer in ('\r','yes\r','risky\r','\x03'):
                with self.subTest(answer=repr(answer)):
                    child=pexpect.spawn(str(BINARY),['-s',str(self.seed),'-K',str(self.hosts),
                        '-p',str(listener.getsockname()[1]),'tester@127.0.0.1'],encoding='utf8',timeout=5)
                    child.expect_exact('Type risk to continue for this connection (Enter cancels): ')
                    child.send(answer);child.expect_exact('Connection cancelled.');child.expect(pexpect.EOF);child.close()
                    self.assertEqual(child.exitstatus,1)
                    with self.assertRaises(socket.timeout):listener.accept()
                    self.assertFalse(self.seed.exists());self.assertFalse(self.hosts.exists())
    def test_password(self):self.run_session()
    def test_saved_host(self):self.run_session(trusted=True)
    def test_changed_host_refuses_credentials(self):self.run_session(reject=True)
    def test_interactive(self):self.run_session('interactive')
    def test_ed25519_encrypted_key(self):self.run_session('key',self.keys['ed25519'])
    def test_rsa_encrypted_key(self):self.run_session('key',self.keys['rsa'])
    def test_ecdsa_encrypted_key(self):self.run_session('key',self.keys['ecdsa'])
    def test_multifactor(self):self.run_session('key',self.keys['ed25519'],partial=True)
    def test_rekey_and_ctr(self):self.run_session(cipher='aes256-ctr',rekey=True)
    def test_missing_corrupt_seed_and_cli(self):
        for args,text,code in [(['-h'],'Usage:',0),(['-p','0','host'],'Usage:',2),
            (['tester@127.0.0.1','-s',str(self.folder/'absent')],'Cannot load',1)]:
            out=subprocess.run([str(BINARY),*args],input=b'\r',capture_output=True,timeout=5)
            self.assertEqual(out.returncode,code);self.assertIn(text,out.stdout.decode())
        self.seed.write_bytes(b'corrupt')
        out=subprocess.run([str(BINARY),'-s',str(self.seed),'tester@127.0.0.1'],input=b'\r',capture_output=True,timeout=5)
        self.assertEqual(out.returncode,1);self.assertIn(b'Cannot load',out.stdout)

if __name__=='__main__':unittest.main(verbosity=2)
