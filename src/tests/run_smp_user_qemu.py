#!/usr/bin/env python3
"""Run SMP user fixtures as init on disposable disks with 1, 2 and 8 CPUs."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
import time

BASE = Path('src/build/smp-user')
FIXTURES = {
    'private_memory': ('SMP memory fixture passed', 'private memory syscall'),
    'shared_memory': ('SMP shared memory fixture passed', 'shared memory syscall'),
    'pipes': ('SMP pipes fixture passed', 'pipe I/O'),
    'stealing': ('SMP AP stealing fixture passed', None),
    'queues': ('SMP user queues fixture passed', None),
    'timers': ('SMP timers fixture passed', 'process-local syscall'),
    'signals': ('SMP masked signals fixture passed', 'signal syscall'),
}


def boot(fixture, cpus):
    log = BASE / f'{fixture}-{cpus}.log'
    log.write_bytes(b'')
    marker, diagnostic = FIXTURES[fixture]
    expected = [marker, '/sbin/init exited with status 0; halting']
    if cpus > 1:
        if diagnostic is not None:
            expected.append(f'{diagnostic} completed on AP ')
        if fixture == 'pipes':
            expected.extend((
                'SMP channel waiter woken by AP', 'pipe vector I/O completed on AP ',
                'local channel wait published from AP', 'blocked syscall resumed on AP ',
            ))
        if fixture == 'stealing' and cpus > 2:
            expected.append('stolen user process resumed on AP ')
        if fixture == 'queues':
            expected.extend(('AP switched local user queue on CPU ', 'BSP stole a queued user process'))
        if fixture == 'timers':
            expected.extend((
                'timer wait submitted on AP ', 'timer wait resumed on AP ', ' via sleep',
            ))
        if fixture == 'signals':
            expected.extend(('signal delivered on AP ', 'signal returned on AP ', ' via exit'))
    command = [
        os.environ.get('QEMU', 'qemu-system-x86_64'),
        '-accel', os.environ.get('QEMU_ACCEL', 'kvm'),
        '-machine', 'q35', '-m', '512M', '-smp', str(cpus),
        '-bios', os.environ.get('OVMF_CODE', '/usr/share/OVMF/OVMF_CODE.fd'),
        '-cdrom', 'src/build/opensillicon.iso',
        '-drive', f'file={BASE / (fixture + ".img")},if=none,id=rootdisk,format=raw,snapshot=on',
        '-device', 'ide-hd,drive=rootdisk,bus=ide.0',
        '-display', 'none', '-serial', f'file:{log}', '-monitor', 'none', '-no-reboot',
    ]
    with log.with_suffix('.qemu.log').open('wb') as errors:
        process = subprocess.Popen(command, stdout=errors, stderr=errors)
        try:
            deadline = time.monotonic() + float(os.environ.get('SMP_TEST_TIMEOUT', '60'))
            while time.monotonic() < deadline:
                output = log.read_text(errors='replace')
                if all(item in output for item in expected):
                    print(f'{fixture}, {cpus} CPUs: PASS', flush=True)
                    return True
                if process.poll() is not None or 'panic:' in output.lower() or (
                    '/sbin/init exited with status ' in output
                ):
                    break
                time.sleep(.1)
            missing = [item for item in expected if item not in output]
            print(f'{fixture}, {cpus} CPUs: FAIL; missing {missing}; see {log}', flush=True)
            return False
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def main():
    fixtures = sys.argv[1:] or list(FIXTURES)
    if any(fixture not in FIXTURES for fixture in fixtures):
        raise SystemExit("Unknown SMP fixture")
    passed = []
    with ThreadPoolExecutor(3) as pool:
        for fixture in fixtures:
            passed.extend(pool.map(lambda cpus: boot(fixture, cpus), (1, 2, 8)))
    raise SystemExit(0 if all(passed) else 1)


if __name__ == '__main__':
    main()
