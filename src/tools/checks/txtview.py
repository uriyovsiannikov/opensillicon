#!/usr/bin/env python3
"""Exercise the real pager in a host PTY, including terminal restoration."""
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import shlex
import signal
import struct
import subprocess
import tempfile
import termios
import time

root = Path(__file__).resolve().parents[3]
# populate_root installs coreutils positionally; keep the two lists identical.
common = (root / "src/mk/common.mk").read_text()
ordered = re.search(r"^COREUTILS := (.+)$", common, re.M).group(1).split()
installer = (root / "src/tools/populate_root.c").read_text()
names = re.search(r'static const char \*names\[\] = \{(.*?)\};', installer, re.S).group(1)
assert ordered == re.findall(r'"([^"]+)"', names), "coreutils installation order mismatch"
with tempfile.TemporaryDirectory(prefix="txtview-") as directory:
    directory = Path(directory)
    binary = directory / "txtview"
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        str(root / "src/user/coreutils/txtview.c"), "-o", str(binary)
    ], check=True)
    log = directory / "log.txt"
    original = "".join(f"line{i:03d} abcdefghijklmnopqrstuvwxyz\r\n" for i in range(40))
    log.write_text(original)

    def session(path, actions, final=b"q", rows=6, columns=48):
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
        saved = termios.tcgetattr(slave)
        process = subprocess.Popen([str(binary), str(path)], stdin=slave, stdout=slave, stderr=slave)
        output = bytearray()

        def expect(needle):
            deadline = time.monotonic() + 4
            start = len(output)
            while time.monotonic() < deadline:
                if select.select([master], [], [], .05)[0]:
                    try:
                        data = os.read(master, 65536)
                    except OSError as error:
                        if error.errno == errno.EIO:
                            break
                        raise
                    output.extend(data)
                if needle in output[start:]:
                    return
                if process.poll() is not None:
                    break
            raise AssertionError((needle, bytes(output[-3000:])))

        try:
            expect(b"col 1")
            for keys, expected in actions:
                os.write(master, keys)
                expect(expected)
            if final is None:
                process.send_signal(signal.SIGTERM)
            else:
                os.write(master, final)
            assert process.wait(timeout=4) == (143 if final is None else 0)
            assert termios.tcgetattr(slave) == saved, "terminal settings were not restored"
            return bytes(output)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)
            os.close(slave)

    session(log, [
        (b"\x1b[B", b"2-6/40"),
        (b"\x1b[6~", b"7-11/40"),
        (b"\x1b[F", b"36-40/40"),
        (b"\x1b[5~", b"31-35/40"),
        (b"\x1b[A", b"30-34/40"),
        (b"\x1b[H", b"1-5/40"),
        (b"\x1b[C", b"col 9"),
        (b"\x1b[D", b"col 1"),
    ])
    session(log, [], final=b"\x1b")
    session(log, [], final=b"\x03")
    session(log, [], final=None)
    assert log.read_bytes() == original.encode(), "viewer modified its input"
    empty = directory / "empty.txt"
    empty.write_bytes(b"")
    session(empty, [(b"\x1b[F", b"1-1/1")])
    unsafe = directory / "controls.txt"
    unsafe.write_bytes(b"safe\x1b[31mBAD\x00\tend\r\nlast")
    output = session(unsafe, [])
    assert b"\x1b[31m" not in output and b"safe?[31mBAD?" in output
    missing = subprocess.run([str(binary), str(directory / "missing")], capture_output=True)
    assert missing.returncode != 0
    print("TXTVIEW_PASS: arrows, pages, home/end, horizontal scrolling, Esc/Ctrl-C, empty/CRLF/control bytes, unchanged file and restored terminal")
