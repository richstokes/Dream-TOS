"""Exercise power recovery and host logging without contacting hardware."""

import contextlib
import errno
import importlib.util
import io
import os
from pathlib import Path
import socket
import struct
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("console_runner", ROOT / "scripts/test-console.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class ConsolePower(unittest.TestCase):
    def test_wrong_plug_never_switches_power(self):
        with patch.object(runner, "rpc", return_value={"id": "other-plug"}) as rpc:
            with self.assertRaisesRegex(RuntimeError, "identity mismatch"):
                runner.power_cycle("plug", runner.DEFAULT_PLUG_ID, 1, Mock())
        self.assertEqual(rpc.call_count, 1)

    def test_lost_off_reply_still_restores_power(self):
        calls = []

        def rpc(host, method, params=None):
            calls.append((method, params))
            if method == "Shelly.GetDeviceInfo":
                return {"id": runner.DEFAULT_PLUG_ID}
            if params.get("on") is False:
                raise TimeoutError("off response was lost")
            return {"output": True}

        with patch.object(runner, "rpc", side_effect=rpc):
            with self.assertRaisesRegex(TimeoutError, "off response"):
                runner.power_cycle("plug", runner.DEFAULT_PLUG_ID, 1, Mock())
        self.assertIn(("Switch.Set", {"id": 0, "on": False, "toggle_after": 1}), calls)
        self.assertIn(("Switch.Set", {"id": 0, "on": True}), calls)
        self.assertEqual(calls[-1], ("Switch.GetStatus", {"id": 0}))

    def test_ctrl_c_during_off_delay_still_restores_power(self):
        replies = [{"id": runner.DEFAULT_PLUG_ID}, {}, {}, {"output": True}]
        with patch.object(runner, "rpc", side_effect=replies) as rpc:
            with patch.object(runner.time, "sleep", side_effect=KeyboardInterrupt):
                with self.assertRaises(KeyboardInterrupt):
                    runner.power_cycle("plug", runner.DEFAULT_PLUG_ID, 1, Mock())
        self.assertEqual(rpc.call_args_list[-2].args,
                         ("plug", "Switch.Set", {"id": 0, "on": True}))

    def test_power_on_is_retried_and_verified(self):
        replies = [{"id": runner.DEFAULT_PLUG_ID}, {}, TimeoutError(),
                   {}, {"output": False}, {}, {"output": True}]
        with patch.object(runner, "rpc", side_effect=replies) as rpc:
            with patch.object(runner.time, "sleep"):
                runner.power_cycle("plug", runner.DEFAULT_PLUG_ID, 1, Mock())
        self.assertEqual(sum(call.args[1:] == ("Switch.Set", {"id": 0, "on": True})
                             for call in rpc.call_args_list), 3)


class LoaderReadiness(unittest.TestCase):
    def test_mac_host_down_during_boot_is_retried(self):
        sock = Mock()
        sock.__enter__ = Mock(return_value=sock)
        sock.__exit__ = Mock(return_value=False)
        version = b"dcload-ip 2.0.3\0"
        sock.send.side_effect = [OSError(errno.EHOSTDOWN, "Host is down"), 12]
        sock.recv.return_value = struct.pack("!4sII", b"VERS", 0o400, len(version)) + version
        report = Mock()
        with patch.object(runner.socket, "socket", return_value=sock):
            with patch.object(runner.time, "sleep"):
                runner.wait_for_loader("127.0.0.1", 53535, 1, report)
        self.assertEqual(sock.send.call_count, 2)
        report.assert_called_with("Loader ready: dcload-ip 2.0.3")

    def test_rejects_truncated_unrelated_and_malformed_packets(self):
        version = b"dcload-ip 2.0.3 using Broadband Adapter\0"
        packet = struct.pack("!4sII", b"VERS", 0o400, len(version)) + version
        self.assertEqual(runner.version_reply(packet), version[:-1].decode())
        for bad in (b"VERS", packet[:-1], b"EXEC" + packet[4:],
                    struct.pack("!4sII", b"VERS", 0, 5) + b"bogus"):
            self.assertIsNone(runner.version_reply(bad))

    def test_wait_requires_loader_reply_not_other_udp_traffic(self):
        sock = Mock()
        sock.__enter__ = Mock(return_value=sock)
        sock.__exit__ = Mock(return_value=False)
        version = b"dcload-ip 2.0.3\0"
        sock.recv.side_effect = [socket.timeout(), b"unrelated",
                                struct.pack("!4sII", b"VERS", 0o400, len(version)) + version]
        report = Mock()
        with patch.object(runner.socket, "socket", return_value=sock):
            runner.wait_for_loader("127.0.0.1", 53535, 1, report)
        self.assertEqual(sock.send.call_count, 3)
        sock.bind.assert_called_once_with(("", 53535))
        report.assert_called_with("Loader ready: dcload-ip 2.0.3")


class ConsoleCapture(unittest.TestCase):
    def test_pty_captures_stdout_and_stderr_with_exit_code(self):
        log = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()):
            code = runner.run_console(
                [sys.executable, "-c", "import sys; print('boot stage'); "
                 "print('exception', file=sys.stderr); sys.exit(7)"], log, None, Mock())
        self.assertEqual(code, 7)
        self.assertIn("boot stage", log.getvalue())
        self.assertIn("exception", log.getvalue())

    def test_time_limit_stops_only_the_spawned_process(self):
        log = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()):
            code = runner.run_console(
                [sys.executable, "-c", "import os,time; print(os.getpid(),flush=True); time.sleep(60)"],
                log, 0.3, Mock())
        self.assertEqual(code, 124)
        pid = int(log.getvalue().strip())
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)

    def test_bad_elf_fails_before_hardware_access(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "game.cdi"
            image.write_bytes(b"not an ELF")
            with patch.object(runner, "rpc") as rpc, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    runner.main(["--dc-tool", sys.executable, "--elf", str(image),
                                 "--map", directory])
            rpc.assert_not_called()


if __name__ == "__main__":
    unittest.main()
