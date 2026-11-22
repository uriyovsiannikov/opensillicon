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
                ('host 127.0.0.1', '127.0.0.1 has address 127.0.0.1'),
                ('host help', 'usage: host'),
                ('ifconfig bad', 'usage: ifconfig'),
                ('uname', 'OpenSillicon\n'),
                ('basename /etc/hosts', 'hosts\n'),
                ('basename /tmp/file.txt .txt', 'file\n'),
                ('dirname /etc/hosts', '/etc\n'),
                ('dirname /', '/\n'),
                ('./bin/uname', 'OpenSillicon\n'),
                ('no-such-command', 'sh: command not found: no-such-command'),
                ('sleep 0', None),
                ('sleep 1', None),
                ('sleep bad', 'usage: sleep whole-seconds'),
                ('ps', 'PID PPID UID STATE COMMAND\n'),
                ('cpuconf', None),
                ('usbconf -l', None),
                ('pciconf -l', None),
                ('blkconf', None),
                ('netconf -l', 'Ethernet'),
                ('audioconf', None),
                ('kldstat', 'e1000'),
                ('netconf bad', 'usage: netconf'),
                ('kill -s 0 1', 'kill: 1: Operation not permitted'),
                ('kill -s bad 1', 'usage: kill'),
                ('kill -s 0 2147483648', 'kill: invalid pid:'),
                ('uname -snrm', 'OpenSillicon opensillicon development amd64\n'),
                ('uname -v', 'OpenSillicon development kernel\n'),
                ('uname bad', 'usage: uname'),
                ('pkg', 'usage: pkg repo URL'),
                ('doas /bin/pkg', 'usage: pkg repo URL'),
                ('echo native stdio check', '\nnative stdio check\n'),
                ('seq 1 2 5', '\n1\n3\n5\n'),
                ('seq 1000000000000 1000000000000', '\n1000000000000\n'),
                ('seq 1 0 5', 'usage: seq'),
                ('true', None),
                ('false', 'sh: command exited status 1'),
                ('touch /tmp/native-openat-check', None),
                ('tail -n 0 /tmp/native-openat-check', None),
                ('tail -n 1 /etc/hosts', '127.0.0.1 localhost'),
                ('tail -n bad', 'usage: tail'),
                ('cat /etc/hosts', '127.0.0.1 localhost'),
                ('cat -u /etc/hosts', '127.0.0.1 localhost'),
                ('cat /tmp/native-openat-check', None),
                ('head -n 1 /etc/hosts', '127.0.0.1 localhost'),
                ('head -n 0 /tmp/native-openat-check', None),
                ('head -n bad', 'usage: head'),
                ('wc /tmp/native-openat-check', '0 0 0 /tmp/native-openat-check'),
                ('wc -l /etc/hosts', '1 /etc/hosts'),
                ('wc -z', 'usage: wc'),
                ('stat /tmp/native-openat-check', '  Mode: 644'),
                ('stat -L /tmp/native-openat-check', '  Type: regular file'),
                ('cd /tmp/native-openat-check/..', 'sh: cd: /tmp/native-openat-check/.. failed'),
                ('cd /tmp/no-such-dir/..', 'sh: cd: /tmp/no-such-dir/.. failed'),
                ('pwd', '/\n'),
                ('pwd -P', '/\n'),
                ('rm /tmp/native-openat-check', None),
                ('mkdir /tmp/dir-check', None),
                ('touch /tmp/dir-check/entry', None),
                ('cd /tmp/dir-check', None),
                ('pwd', '/tmp/dir-check\n'),
                ('stat ent\t', '  Type: regular file'),
                ('cd /', None),
                ('help', 'Available commands:'),
                ('whic\tls', '/bin/ls'),
                ('which ./bin/ls', './bin/ls'),
                ('which no-such-command', 'sh: command exited status 1'),
                ('stat /tmp/dir-che\tentry', '  Type: regular file'),
                ('ls /tmp/dir-check', 'entry\n'),
                ('rm /tmp/dir-check', 'is a directory'),
                ('ls /tmp/dir-check', 'entry\n'),
                ('rm -r /tmp/dir-check', None),
                ('stat /tmp/dir-check', 'stat: cannot stat:'),
                ('rm -f /tmp/dir-check', None),
                ('doas /bin/pkg list -i', 'Installed packages (0)'),
                ('doas /bin/pkg repo https://127.0.0.1:1', 'Repository saved in /etc/pkg.conf'),
                ('ifconfig', 'inet 10.15.3.15 netmask 255.255.255.0 gateway 10.15.3.2'),
                ('doas /bin/sysupgrade check', 'sysupgrade: unable to fetch a verified release'),
            ]:
                offset = len(log.read_text(errors='replace'))
                for char in line + '\n':
                    key = {' ': 'spc', '\n': 'ret', '\t': 'tab', '/': 'slash', '-': 'minus', '.': 'dot', ':': 'shift-semicolon'}.get(char, char)
                    if 'A' <= char <= 'Z':
                        key = 'shift-' + char.lower()
                    command('human-monitor-command', {'command-line': 'sendkey ' + key + ' 1'})
                    time.sleep(.03)
                end = wait_for(expected, offset) if expected is not None else offset
                # Kernel diagnostics can appear between the cwd and prompt writes.
                prompt = wait_for('# ', end)
                finished = wait_for('% ', prompt)
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
