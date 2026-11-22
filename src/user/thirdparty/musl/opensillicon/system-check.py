#!/usr/bin/env python3
"""Boot the actual Live ISO and exercise programs from its normal user shell."""
from pathlib import Path
import json
import socket
import subprocess
import tempfile
import time

out = Path('src/build/musl-system-check').resolve()
out.mkdir(parents=True, exist_ok=True)
log = out / 'serial.log'
log.unlink(missing_ok=True)
with tempfile.TemporaryDirectory(prefix='musl-qmp-') as temporary:
    qmp_path = str(Path(temporary) / 'qmp.sock')
    q = subprocess.Popen([
        'qemu-system-x86_64', '-accel', 'kvm', '-machine', 'q35',
        '-cpu', 'host', '-m', '1024', '-smp', '2',
        '-bios', '/usr/share/OVMF/OVMF_CODE.fd',
        '-cdrom', 'src/build/opensillicon-live.iso',
        '-netdev', 'user,id=net0,net=10.15.3.0/24,host=10.15.3.2,dhcpstart=10.15.3.15',
        '-device', 'e1000,netdev=net0', '-display', 'none',
        '-serial', 'file:' + str(log), '-qmp', 'unix:' + qmp_path + ',server=on,wait=off',
        '-no-reboot'], stderr=subprocess.PIPE)
    def wait_for(expected, offset=0, timeout=40):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = log.read_text(errors='replace') if log.exists() else ''
            found = text.find(expected, offset)
            if found >= 0:
                return found + len(expected)
            if q.poll() is not None or '/sbin/init exited' in text:
                raise AssertionError(text)
            time.sleep(.1)
        raise AssertionError('Missing ' + expected + '\n' + text[-5000:])
    try:
        wait_for('/% ')
        with socket.socket(socket.AF_UNIX) as control:
            control.settimeout(5)
            control.connect(qmp_path)
            stream = control.makefile('rwb', buffering=0)
            stream.readline()
            def command(name, args=None):
                stream.write((json.dumps({'execute': name, 'arguments': args or {}}) + '\n').encode())
                while True:
                    response = json.loads(stream.readline())
                    if 'return' in response:
                        return response['return']
                    if 'error' in response:
                        raise RuntimeError(response)
            command('qmp_capabilities')
            for line, expected in [
                ('svctl list', 'console: running pid='),
                ('host localhost', 'localhost has address 127.0.0.1'),
                ('pkg', 'usage: pkg repo URL'),
                ('doas /bin/pkg', 'usage: pkg repo URL'),
                ('touch /tmp/native-openat-check', None),
                ('stat /tmp/native-openat-check', '  Mode: 644'),
                ('rm /tmp/native-openat-check', None),
                ('mkdir /tmp/dir-check', None),
                ('touch /tmp/dir-check/entry', None),
                ('ls /tmp/dir-check', 'entry\n'),
                ('rm /tmp/dir-check', 'is a directory'),
                ('ls /tmp/dir-check', 'entry\n'),
                ('rm -r /tmp/dir-check', None),
                ('stat /tmp/dir-check', 'stat: cannot stat:'),
                ('rm -f /tmp/dir-check', None),
                ('doas /bin/pkg list -i', 'Installed packages (0)'),
                ('doas /bin/pkg repo https://127.0.0.1:1', 'Repository saved in /etc/pkg.conf'),
                ('doas /bin/sysupgrade check', 'sysupgrade: unable to fetch a verified release'),
            ]:
                offset = len(log.read_text(errors='replace'))
                for char in line + '\n':
                    key = {' ': 'spc', '\n': 'ret', '/': 'slash', '-': 'minus', '.': 'dot', ':': 'shift-semicolon'}.get(char, char)
                    command('human-monitor-command', {'command-line': 'sendkey ' + key + ' 1'})
                    time.sleep(.03)
                end = wait_for(expected, offset) if expected is not None else offset
                # Kernel diagnostics can appear between the cwd and prompt writes.
                prompt = wait_for('# ', end)
                finished = wait_for('/% ', prompt)
                if expected is None:
                    output = log.read_text(errors='replace')[offset:finished]
                    assert 'sh: command exited status' not in output, output
            command('screendump', {'filename': str(out / 'console.png'), 'format': 'png'})
        print('MUSL_SYSTEM_PASS: init, console, svctl, host, pkg, doas and native openat')
    finally:
        q.terminate()
        try:
            q.wait(timeout=5)
        except subprocess.TimeoutExpired:
            q.kill()
            q.wait()
