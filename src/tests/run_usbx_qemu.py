from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import os
import socket
import subprocess
import tempfile
import time

base = Path('src/build/usbx-check')
ready = 'USBX host core ready (Hub, HID, Storage)'
attached = 'xHCI command ring verified and attached to USBX'
cases = (
    ('no-controller', 1, [], ['no USB PCI host controller found']),
    ('ehci', 2, ['ich9-usb-ehci1,id=ehci'], ['USB PCI host controller detected: EHCI', ready]),
    ('xhci-hotplug', 2, ['qemu-xhci,id=xhci,p3=0'], [ready, attached]),
    ('xhci-empty', 2, ['qemu-xhci,id=xhci'], [ready, attached]),
    ('xhci-kbd', 8, ['qemu-xhci,id=xhci', 'usb-kbd,bus=xhci.0', 'usb-mouse,bus=xhci.0'],
     [ready, attached, 'USBX enumerated VID 1575 PID 1', 'USBX keyboard ready',
      'PS/2 keyboard unavailable', 'USBX test cursor key pressed and released',
      'USBX test console read succeeded', 'USBX test mouse movement and buttons succeeded']),
    ('xhci-storage', 2, ['qemu-xhci,id=xhci,p3=0', 'usb-storage,bus=xhci.0,drive=usbtest'],
     [ready, attached, 'USBX enumerated VID 18164 PID 1']),
    ('xhci-two-hid', 2, ['qemu-xhci,id=xhci,p3=0', 'usb-kbd,bus=xhci.0', 'usb-mouse,bus=xhci.0'],
     [ready, attached, 'USBX enumerated VID 1575 PID 1 root port 1',
      'USBX enumerated VID 1575 PID 1 root port 2']),
)

def qmp_command(connection, command, **arguments):
    request = {'execute': command, 'arguments': arguments}
    connection.write(json.dumps(request).encode() + b'\n')
    while line := connection.readline():
        result = json.loads(line)
        if 'error' in result:
            raise RuntimeError(result['error'])
        if 'return' in result:
            return
    raise RuntimeError('QMP connection closed')


def wait_marker(read_log, marker, start=0):
    deadline = time.monotonic() + 3
    while marker not in read_log()[start:]:
        if time.monotonic() >= deadline:
            raise RuntimeError(f'Missing guest marker: {marker}')
        time.sleep(0.02)


def exercise_keyboard(connection, read_log):
    def send(*keys):
        qmp_command(connection, 'input-send-event', events=[
            {'type': 'key', 'data': {'key': {'type': 'qcode', 'data': key}, 'down': down}}
            for key, down in keys
        ])

    wait_marker(read_log, 'USBX keyboard ready')
    # Each key pair creates two reports. Send enough to wrap the 63-entry
    # transfer ring, then verify both directions of the final cursor event.
    for _ in range(36):
        send(('b', True))
        time.sleep(0.02)
        send(('b', False))
        time.sleep(0.02)
    send(('up', True), ('up', False))
    wait_marker(read_log, 'USBX test cursor key pressed and released')
    send(('shift', True), ('a', True), ('a', False), ('shift', False),
         ('ret', True), ('ret', False))

    wait_marker(read_log, 'USBX test console read succeeded')
    wait_marker(read_log, 'USBX mouse ready')
    qmp_command(connection, 'input-send-event', events=[
        {'type': 'rel', 'data': {'axis': 'x', 'value': 12}},
        {'type': 'rel', 'data': {'axis': 'y', 'value': -7}},
        {'type': 'btn', 'data': {'button': 'left', 'down': True}},
    ])
    wait_marker(read_log, 'USBX test mouse button pressed')
    qmp_command(connection, 'input-send-event', events=[
        {'type': 'btn', 'data': {'button': 'left', 'down': False}},
    ])


def hotplug(connection, read_log):
    for operation in ('device_add', 'device_del', 'device_add'):
        start = len(read_log())
        if operation == 'device_add':
            qmp_command(connection, operation, driver='usb-kbd', id='hotkbd',
                        bus='xhci.0', port='1')
            wait_marker(read_log, 'USBX enumerated VID 1575 PID 1 root port 1', start)
            wait_marker(read_log, 'USBX keyboard ready', start)
        else:
            qmp_command(connection, operation, id='hotkbd')
            wait_marker(read_log, 'USBX device disconnected', start)


def exercise_vm(path, read_log, exercise):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(3)
        sock.connect(path)
        with sock.makefile('rwb', buffering=0) as connection:
            greeting = json.loads(connection.readline())
            if 'QMP' not in greeting:
                raise RuntimeError('Missing QMP greeting')
            qmp_command(connection, 'qmp_capabilities')
            exercise(connection, read_log)


def boot(case):
    name, cpus, devices, markers = case
    expected = [*markers, '/sbin/init exited with status 0; halting']
    log = base / f'{name}.log'
    log.write_bytes(b'')
    root = 'keyboard' if name == 'xhci-kbd' else 'root'
    args = [os.environ.get('QEMU', 'qemu-system-x86_64'),
            '-accel', os.environ.get('QEMU_ACCEL', 'kvm'), '-machine', 'q35,i8042=off' if name == 'xhci-kbd' else 'q35',
            '-m', '512M', '-smp', str(cpus),
            '-bios', os.environ.get('OVMF_CODE', '/usr/share/OVMF/OVMF_CODE.fd'),
            '-cdrom', 'src/build/opensillicon.iso',
            '-drive', f'file={base}/{root}.img,if=none,id=disk,format=raw,snapshot=on',
            '-device', 'ide-hd,drive=disk,bus=ide.0', '-display', 'none', '-monitor', 'none',
            '-serial', f'file:{log}', '-no-reboot']
    if name == 'xhci-storage':
        args += ['-drive', f'file={base}/storage.img,if=none,id=usbtest,format=raw,snapshot=on']
    for device in devices:
        args += ['-device', device]
    with tempfile.TemporaryDirectory(prefix='usbx-') as work, \
            (base / f'{name}.qemu.log').open('wb') as stderr:
        qmp_path = f'{work}/qmp'
        exercise = {'xhci-hotplug': hotplug, 'xhci-kbd': exercise_keyboard}.get(name)
        if exercise is not None:
            args += ['-qmp', f'unix:{qmp_path},server=on,wait=off']
        with subprocess.Popen(args, stdout=stderr, stderr=stderr) as vm:
            try:
                tested = exercise is None
                deadline = time.monotonic() + 40
                while time.monotonic() < deadline and vm.poll() is None:
                    output = log.read_text(errors='replace')
                    if not tested and 'USBX test init ready' in output:
                        try:
                            exercise_vm(qmp_path, lambda: log.read_text(errors='replace'), exercise)
                        except (OSError, RuntimeError, ValueError) as error:
                            print(f'{name}: FAIL ({error})', flush=True)
                            return False
                        tested = True
                        continue
                    if tested and all(marker in output for marker in expected):
                        print(f'{name}, {cpus} CPUs: PASS', flush=True)
                        return True
                    if any('/sbin/init exited with status ' in line and line.endswith('halting')
                           for line in output.splitlines()):
                        break
                    time.sleep(0.1)
                print(f'{name}, {cpus} CPUs: FAIL; see {log}', flush=True)
                return False
            finally:
                if vm.poll() is None:
                    vm.terminate()
                    try:
                        vm.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        vm.kill()
                        vm.wait()

if __name__ == '__main__':
    with (base / 'storage.img').open('wb') as disk:
        disk.truncate(1024 * 1024)
    with ThreadPoolExecutor(3) as pool:
        passed = list(pool.map(boot, cases))
    raise SystemExit(not all(passed))
