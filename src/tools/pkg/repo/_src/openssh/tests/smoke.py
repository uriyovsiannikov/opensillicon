#!/usr/bin/env python3
"""Boot OpenSillicon and exercise ssh startup and Ed25519 key generation."""
from pathlib import Path
import os
import pwd
import socket
import tempfile
import shutil
import subprocess
import time

root = Path(__file__).resolve().parents[7]
os.chdir(root)
out = root / 'src/build/openssh-smoke'
out.mkdir(parents=True, exist_ok=True)
def run(*args):
    subprocess.run([str(x) for x in args], check=True, stdout=subprocess.DEVNULL)
run('make', '-j4', 'kernel', 'src/build/populate_root', 'src/build/shell.elf', 'src/build/initramfs.cpio', 'src/build/modules/e1000.ko')
port = root / 'src/build/openssh-port'
fixture = tempfile.TemporaryDirectory(prefix='openssh-test-', dir=out)
host = Path(fixture.name)
run('ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', host/'key')
run('ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', host/'client')
shutil.copyfile(host/'client.pub', host/'authorized_keys')
with socket.socket() as probe:
    probe.bind(('127.0.0.1', 0))
    listen_port = probe.getsockname()[1]
user = pwd.getpwuid(os.getuid()).pw_name
(host/'sshd_config').write_text(f"Port {listen_port}\nListenAddress 127.0.0.1\nHostKey {host}/key\n"
    f"PidFile {host}/sshd.pid\nAuthorizedKeysFile {host}/authorized_keys\nStrictModes no\n"
    "PasswordAuthentication no\nKbdInteractiveAuthentication no\nUsePAM no\nPrintMotd no\n"
    "Subsystem sftp /usr/libexec/openssh/sftp-server\n")
(host/'ssh_config').write_text(f"Host testhost\n HostName 10.0.2.2\n Port {listen_port}\n User {user}\n"
    " IdentityFile /tmp/clientkey\n UserKnownHostsFile /tmp/known_hosts\n"
    " GlobalKnownHostsFile /dev/null\n StrictHostKeyChecking yes\n BatchMode yes\n AddressFamily inet\n")
(host/'known_hosts').write_text(f'[10.0.2.2]:{listen_port} ' + (host/'key.pub').read_text())
(host/'bad_hosts').write_text(f'[10.0.2.2]:{listen_port} ' + (host/'client.pub').read_text())
(host/'bad_config').write_text((host/'ssh_config').read_text().replace('/tmp/known_hosts', '/tmp/bad_hosts'))
(host/'destination').write_text(f'testhost:{host}/uploaded.txt')
blobs = [('server', port/'sshd'), ('agent', port/'ssh-agent'), ('add', port/'ssh-add'), ('badhosts', host/'bad_hosts'), ('badconfig', host/'bad_config'), ('scp', port/'scp'), ('destination', host/'destination'), ('ssh', port/'ssh'), ('keygen', port/'ssh-keygen'), ('clientkey', host/'client'), ('sshconfig', host/'ssh_config'), ('knownhosts', host/'known_hosts')]
(out/'blobs.S').write_text('.section .rodata\n' + ''.join(
    f'.global {name}_start,{name}_end\n{name}_start: .incbin "{path}"\n{name}_end:\n'
    for name, path in blobs) + '.section .note.GNU-stack,"",@progbits\n')
cc = os.environ.get('CC', 'gcc')
headers = subprocess.check_output([cc, '-print-file-name=include'], text=True).strip()
run(cc, '-O2', '-ffreestanding', '-fno-pie', '-fno-builtin', '-fno-stack-protector',
    '-mno-red-zone', '-nostdinc', '-Isrc/include', '-Isrc/build/musl/sysroot/include',
    '-isystem', headers, '-c', Path(__file__).with_name('smoke.c'), '-o', out/'init.o')
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
server_log = (host/'server.log').open('w')
server = subprocess.Popen(['/usr/bin/sshd', '-D', '-e', '-f', str(host/'sshd_config')],
                          stdout=server_log, stderr=subprocess.STDOUT)
qemu = subprocess.Popen(['qemu-system-x86_64', '-enable-kvm', '-cpu', 'host', '-m', '512',
    '-smp', '2', '-bios', '/usr/share/OVMF/OVMF_CODE.fd', '-cdrom', str(out/'check.iso'),
    '-netdev', 'user,id=net0', '-device', 'e1000,netdev=net0', '-device', 'virtio-rng-pci', '-display', 'none', '-serial', 'file:'+str(log), '-no-reboot'])
try:
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        text = log.read_text(errors='replace') if log.exists() else ''
        if 'OPENSSH_SMOKE_PASS' in text or '/sbin/init exited' in text or qemu.poll() is not None:
            break
        time.sleep(.5)
    print(text)
    if ('OPENSSH_SMOKE_PASS' not in text or 'OPENSSH_REMOTE_PASS' not in text
            or 'SSH_TEST_REJECTED_HOST_KEY' not in text or 'SSH_TEST_AGENT_PASS' not in text
            or 'SSH_SERVER_CONFIG_PASS' not in text):
        raise SystemExit('OpenSSH smoke check failed: '+str(log))
    if (host/'uploaded.txt').read_text() != 'OpenSillicon SCP transfer\n':
        raise SystemExit('SCP transfer content mismatch')
finally:
    qemu.terminate()
    try: qemu.wait(timeout=5)
    except subprocess.TimeoutExpired: qemu.kill(); qemu.wait()
    server.terminate()
    try: server.wait(timeout=5)
    except subprocess.TimeoutExpired: server.kill(); server.wait()
    server_log.close()
    fixture.cleanup()
