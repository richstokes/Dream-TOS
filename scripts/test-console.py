#!/usr/bin/env python3
"""Power-cycle the test Dreamcast, wait for dcload-ip, and stream a logged run.

The default run stays attached: dc-tool-ip also serves the mapped /pc/ files.
Ctrl+C stops the host tool and leaves the console powered on. No build is run.
"""

import argparse
import codecs
from datetime import datetime
import errno
import json
import os
from pathlib import Path
import pty
import select
import shlex
import signal
import socket
import struct
import subprocess
import sys
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PLUG_ID = "shellyplugusg4-e8f60a7dd06c"
VERSION_REQUEST = struct.pack("!4sII", b"VERS", 0x020003, 0)


def rpc(host, method, params=None):
    """Use the local Shelly RPC API, bypassing HTTP proxy environment settings."""
    body = None if params is None else json.dumps(params).encode("utf-8")
    request = urllib.request.Request(
        "http://%s/rpc/%s" % (host, method), data=body,
        headers={"Content-Type": "application/json"},
    )
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=3) as response:
        result = json.load(response)
    if not isinstance(result, dict) or "error" in result or "code" in result:
        raise RuntimeError("Shelly %s failed: %r" % (method, result))
    return result


def power_cycle(host, expected_id, off_seconds, report):
    info = rpc(host, "Shelly.GetDeviceInfo")
    if info.get("id") != expected_id:
        raise RuntimeError("Shelly identity mismatch: expected %s, got %r" %
                           (expected_id, info.get("id")))
    report("Power cycling %s (%s), off for %gs" %
           (expected_id, host, off_seconds))
    try:
        # The plug restores power itself even if this process/network disappears.
        # https://shelly-api-docs.shelly.cloud/gen2/ComponentsAndServices/Switch
        rpc(host, "Switch.Set", {"id": 0, "on": False,
                                 "toggle_after": off_seconds})
        time.sleep(off_seconds)
    finally:
        # Also explicitly restore power if the off response or sleep failed.
        for attempt in range(3):
            try:
                rpc(host, "Switch.Set", {"id": 0, "on": True})
                if rpc(host, "Switch.GetStatus", {"id": 0}).get("output") is not True:
                    raise RuntimeError("Shelly did not confirm power on")
                break
            except (OSError, ValueError, RuntimeError):
                if attempt == 2:
                    raise RuntimeError("Could not confirm power on; the plug's "
                                       "automatic restore timer was requested")
                time.sleep(0.2)
    report("Power is on")


def version_reply(packet):
    if len(packet) < 12:
        return None
    command, _adapter, size = struct.unpack("!4sII", packet[:12])
    if command != b"VERS" or not 0 < size <= len(packet) - 12:
        return None
    version = packet[12:12 + size].rstrip(b"\0")
    if not version.startswith(b"dcload-ip "):
        return None
    return version.decode("ascii", "replace")


def wait_for_loader(host, port, timeout, report):
    """Check the actual loader, not just ping; do not upload or execute a probe.

    Wire format and fixed reply port verified against KallistiOS/dcload-ip:
    host-src/tool/commands.h and target-src/dcload/commands.c (cmd_version).
    VERS negotiates v2 protocol settings; dc-tool repeats that negotiation.
    https://github.com/KallistiOS/dcload-ip
    """
    report("Waiting up to %gs for dcload-ip at %s:%s" % (timeout, host, port))
    deadline = time.monotonic() + timeout
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        try:
            sock.bind(("", port))
        except OSError as error:
            if error.errno == errno.EADDRINUSE:
                raise RuntimeError("UDP port %s is busy; stop the previous "
                                   "dc-tool-ip session first" % port) from error
            raise
        sock.connect((host, port))
        while time.monotonic() < deadline:
            sock.settimeout(min(1, max(0.01, deadline - time.monotonic())))
            try:
                sock.send(VERSION_REQUEST)
                version = version_reply(sock.recv(2048))
                if version:
                    report("Loader ready: " + version)
                    return
            except OSError as error:
                if not isinstance(error, socket.timeout):
                    # ICMP unreachable is normal while the console is booting.
                    if error.errno not in (errno.ECONNREFUSED, errno.EHOSTUNREACH,
                                           errno.EHOSTDOWN, errno.ENETUNREACH,
                                           errno.ETIMEDOUT):
                        raise
                    time.sleep(min(1, max(0, deadline - time.monotonic())))
    raise TimeoutError("dcload-ip did not answer within %gs" % timeout)


def run_console(command, log, run_seconds, report):
    report("Running: " + shlex.join(command))
    report("Keeping dc-tool-ip attached for console output and /pc/ files; Ctrl+C stops it")
    # A PTY makes dc-tool flush its progress/log messages promptly on macOS.
    master, slave = pty.openpty()
    process = None
    decoder = codecs.getincrementaldecoder("utf-8")("replace")
    started = time.monotonic()
    try:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=slave,
                                   stderr=slave, start_new_session=True)
        os.close(slave)
        slave = None
        while True:
            if run_seconds and time.monotonic() - started >= run_seconds:
                report("Run time limit reached; stopping the host console/fileserver")
                return 124
            if not select.select([master], [], [], 0.25)[0]:
                continue
            try:
                chunk = os.read(master, 65536)
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                chunk = b""
            text = decoder.decode(chunk, final=not chunk)
            sys.stdout.write(text)
            sys.stdout.flush()
            log.write(text)
            log.flush()
            if not chunk:
                return process.wait()
    finally:
        if slave is not None:
            os.close(slave)
        os.close(master)
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def positive(value):
    number = float(value)
    if not 0 < number < float("inf"):
        raise argparse.ArgumentTypeError("must be a positive finite number")
    return number


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console", default="192.168.1.171", help="Dreamcast IPv4 address")
    parser.add_argument("--port", type=int, default=53535, help="dcload-ip UDP port")
    parser.add_argument("--plug", default="192.168.1.173", help="Shelly IPv4 address")
    parser.add_argument("--plug-id", default=DEFAULT_PLUG_ID, help="expected Shelly device ID")
    parser.add_argument("--off-seconds", type=positive, default=1)
    parser.add_argument("--boot-timeout", type=positive, default=60,
                        help="maximum loader boot wait (default: 60 seconds)")
    parser.add_argument("--no-power-cycle", action="store_true", help="use an already booted loader")
    parser.add_argument("--power-only", action="store_true", help="wait for loader, then exit without uploading")
    parser.add_argument("--elf", type=Path, default=ROOT / "build/emutos-dreamcast.elf")
    parser.add_argument("--dc-tool", type=Path, default=Path(
        "~/Dropbox/Games/ROMs/DREAMCAST/dcload-ip/dc-tool-ip"))
    parser.add_argument("--map", type=Path, default=ROOT / "build/disc",
                        help="host directory mapped to /pc/ (default: build/disc)")
    parser.add_argument("--log", type=Path, help="log path (default: timestamped build/console-*.log)")
    parser.add_argument("--run-seconds", type=positive,
                        help="stop the host server after this many seconds (exit 124); default stays attached")
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    for attr in ("elf", "dc_tool", "map"):
        setattr(args, attr, getattr(args, attr).expanduser().resolve())
    if not args.power_only:
        if not args.dc_tool.is_file() or not os.access(args.dc_tool, os.X_OK):
            parser.error("dc-tool-ip is not executable: %s" % args.dc_tool)
        if not args.elf.is_file():
            parser.error("ELF not found: %s (run scripts/build.sh first)" % args.elf)
        with args.elf.open("rb") as elf:
            if elf.read(4) != b"\x7fELF":
                parser.error("--elf must be an ELF executable, not a CDI")
        if not args.map.is_dir():
            parser.error("mapped directory not found: %s" % args.map)
    return args


def main(argv=None):
    args = parse_args(argv)
    log_path = args.log or ROOT / ("build/console-%s.log" %
                                 datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
    log_path = log_path.expanduser().resolve()
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("a", encoding="utf-8") as log:
        def report(message):
            line = "[%s] %s\n" % (datetime.now().astimezone().isoformat(timespec="seconds"), message)
            print(line, end="", flush=True)
            log.write(line)
            log.flush()

        report("Log: " + str(log_path))
        try:
            if not args.no_power_cycle:
                power_cycle(args.plug, args.plug_id, args.off_seconds, report)
            wait_for_loader(args.console, args.port, args.boot_timeout, report)
            if args.power_only:
                return 0
            command = [str(args.dc_tool), "-t", "%s:%s" % (args.console, args.port),
                       "-m", str(args.map), "-x", str(args.elf)]
            return run_console(command, log, args.run_seconds, report)
        except KeyboardInterrupt:
            report("Stopped; the console remains powered on")
            return 130
        except (OSError, ValueError, RuntimeError) as error:
            report("ERROR: " + str(error))
            return 1


if __name__ == "__main__":
    def interrupted(_signum, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupted)
    sys.exit(main())
