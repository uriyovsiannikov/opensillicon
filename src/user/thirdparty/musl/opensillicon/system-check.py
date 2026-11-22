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
            if expected in text[offset:]:
                return len(text)
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
            ]:
                offset = len(log.read_text(errors='replace'))
                for char in line + '\n':
                    key = {' ': 'spc', '\n': 'ret', '/': 'slash'}.get(char, char)
                    command('human-monitor-command', {'command-line': 'sendkey ' + key + ' 1'})
                    time.sleep(.03)
                end = wait_for(expected, offset)
                wait_for('/% ', end)
            command('screendump', {'filename': str(out / 'console.png'), 'format': 'png'})
        print('MUSL_SYSTEM_PASS: init, console, svctl, host, pkg and doas')
    finally:
        q.terminate()
        try:
            q.wait(timeout=5)
        except subprocess.TimeoutExpired:
            q.kill()
            q.wait()
