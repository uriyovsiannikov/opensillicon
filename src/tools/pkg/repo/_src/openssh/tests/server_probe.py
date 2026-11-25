#!/usr/bin/env python3
"""Experimental unprivileged server probe; not a privilege separation test."""
from pathlib import Path
import os
import socket
import tempfile
import shutil
import subprocess
import time

root = Path(__file__).resolve().parents[7]
os.chdir(root)
out = root / 'src/build/openssh-server-probe'
out.mkdir(parents=True, exist_ok=True)
def run(*args):
    subprocess.run([str(x) for x in args], check=True, stdout=subprocess.DEVNULL)
run('make', '-j4', 'kernel', 'src/build/populate_root', 'src/build/shell.elf', 'src/build/initramfs.cpio', 'src/build/modules/e1000.ko', 'src/build/coreutils/echo.elf')
port = root / 'src/build/openssh-port'
fixture = tempfile.TemporaryDirectory(prefix='server-test-', dir=out)
host = Path(fixture.name)
run('ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', host/'key')
run('ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', host/'client')
run('ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', host/'unauthorized')
with socket.socket() as probe:
    probe.bind(('127.0.0.1', 0))
    listen_port = probe.getsockname()[1]
(host/'known_hosts').write_text(f'[127.0.0.1]:{listen_port} ' + (host/'key.pub').read_text())
blobs = [('echo', root/'src/build/coreutils/echo.elf'), ('daemon', port/'sshd'), ('session', port/'sshd-session'), ('auth', port/'sshd-auth'),
         ('hostkey', host/'key'), ('authorized', host/'client.pub')]
(out/'blobs.S').write_text('.section .rodata\n' + ''.join(
    f'.global {name}_start,{name}_end\n{name}_start: .incbin "{path}"\n{name}_end:\n'
    for name, path in blobs) + '.section .note.GNU-stack,"",@progbits\n')
cc = os.environ.get('CC', 'gcc')
headers = subprocess.check_output([cc, '-print-file-name=include'], text=True).strip()
run(cc, '-O2', '-ffreestanding', '-fno-pie', '-fno-builtin', '-fno-stack-protector',
    '-mno-red-zone', '-nostdinc', '-Isrc/include', '-Isrc/build/musl/sysroot/include',
    '-isystem', headers, '-c', Path(__file__).with_name('server_probe.c'), '-o', out/'init.o')
run(cc, '-c', out/'blobs.S', '-o', out/'blobs.o')
run(cc, '-nostdlib', '-static', '-no-pie', '-Wl,-z,noexecstack', '-T', 'src/user/c_linker.ld',
    'src/build/musl/sysroot/lib/crt1.o', out/'init.o', out/'blobs.o',
    'src/build/musl/sysroot/lib/libc.a', '-lgcc', '-o', out/'init.elf')
run('python3', 'src/tools/packers/mksilliconfs.py', out/'root.img', '--inodes', '1024',
    '--reserve-mib', '32', '--size-from', out/'init.elf')
run('src/build/populate_root', out/'root.img', out/'init.elf', 'src/build/shell.elf', '--module', 'src/build/modules/e1000.ko')
iso = out/'iso'
(iso/'EFI/BOOT').mkdir(parents=True, exist_ok=True)
(iso/'boot').mkdir(exist_ok=True)
for source, dest in [('src/limine/BOOTX64.EFI', 'EFI/BOOT/BOOTX64.EFI'),
                     ('src/limine/limine-uefi-cd.bin', 'boot/limine-uefi-cd.bin'),
                     ('src/build/kernel.elf', 'boot/kernel.elf'),
                     ('src/build/initramfs.cpio', 'boot/initramfs.cpio'),
                     (out/'root.img', 'boot/root.img')]:
    shutil.copyfile(source, iso/dest)
(iso/'limine.conf').write_text('timeout: 0\n/OpenSSH check\n protocol: limine\n'
    ' path: boot():/boot/kernel.elf\n module_path: boot():/boot/initramfs.cpio\n'
    ' module_string: initramfs\n module_path: boot():/boot/root.img\n'
    ' module_string: live-root\n resolution: 1024x768x32\n')
run('xorriso', '-as', 'mkisofs', '-R', '-r', '-J', '--efi-boot', 'boot/limine-uefi-cd.bin',
    '-efi-boot-part', '--efi-boot-image', '--protective-msdos-label', iso, '-o', out/'check.iso')
log = out/'serial.log'
log.unlink(missing_ok=True)
qemu = subprocess.Popen(['qemu-system-x86_64', '-enable-kvm', '-cpu', 'host', '-m', '512',
    '-smp', '2', '-bios', '/usr/share/OVMF/OVMF_CODE.fd', '-cdrom', str(out/'check.iso'),
    '-netdev', f'user,id=net0,hostfwd=tcp:127.0.0.1:{listen_port}-:2222',
    '-device', 'e1000,netdev=net0', '-display', 'none', '-serial', 'file:'+str(log), '-no-reboot'])
try:
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        text = log.read_text(errors='replace') if log.exists() else ''
        if 'SSH_SERVER_START' in text or '/sbin/init exited' in text or qemu.poll() is not None:
            break
        time.sleep(.2)
    if 'SSH_SERVER_START' not in text:
        raise SystemExit('Server did not start: '+text)
    time.sleep(5)
    def connect(identity):
        return subprocess.run(['ssh', '-F', '/dev/null', '-p', str(listen_port),
            '-i', str(identity), '-o', 'IdentitiesOnly=yes', '-o', 'BatchMode=yes',
            '-o', 'StrictHostKeyChecking=yes', '-o', 'UserKnownHostsFile='+str(host/'known_hosts'),
            '-o', 'ConnectTimeout=5', 'user@127.0.0.1', 'echo SSH_SERVER_REMOTE_PASS'],
            capture_output=True, text=True, timeout=20)
    rejected = connect(host/'unauthorized')
    if rejected.returncode != 255 or 'Permission denied' not in rejected.stderr:
        raise SystemExit('Server failed to reject unauthorized key: '+rejected.stderr)
    print('SSH_SERVER_UNAUTHORIZED_REJECTED')
    result = connect(host/'client')
    print('HOST SSH:', result.returncode, result.stdout, result.stderr)
    print(log.read_text(errors='replace'))
    if result.returncode != 0 or 'SSH_SERVER_REMOTE_PASS' not in result.stdout:
        raise SystemExit('Server probe failed: '+str(log))
finally:
    qemu.terminate()
    try: qemu.wait(timeout=5)
    except subprocess.TimeoutExpired: qemu.kill(); qemu.wait()
    fixture.cleanup()
