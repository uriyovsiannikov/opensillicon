#!/usr/bin/env python3
"""Boot the real kernel and miniJVM and compare JIT with the interpreter.
Requires built kernel.elf, minijvm/java, minijvm_rt.jar and populate_root.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
os.chdir(ROOT)
OUT = ROOT / 'src/build/jit-check'
TESTS = ROOT / 'src/tools/pkg/repo/_src/minijvm/opensillicon/tests'
CLIENT = ROOT / 'src/tools/pkg/repo/_src/minecraft/client.jar'
OUT.mkdir(parents=True, exist_ok=True)

def run(*args):
    subprocess.run([str(x) for x in args], check=True)

run('javac', '--release', '8', '-cp', CLIENT, '-d', OUT,
    TESTS/'JitProbe.java', TESTS/'JitTerrainProbe.java', TESTS/'JitTextProbe.java', TESTS/'JitMapProbe.java', TESTS/'JitGetterProbe.java')
run('jar', 'cf', OUT/'probe.jar', '-C', OUT, 'JitProbe.class',
    '-C', OUT, 'JitProbe$1.class', '-C', OUT, 'JitProbe$Box.class', '-C', OUT, 'JitTerrainProbe.class', '-C', OUT, 'JitTextProbe.class', '-C', OUT, 'JitMapProbe.class')
run('jar','uf',OUT/'probe.jar', *[x for p in sorted(OUT.glob('JitGetterProbe*.class')) for x in ('-C',OUT,p.name)])
reference = subprocess.check_output(['java', '-cp', str(OUT/'probe.jar')+':'+str(CLIENT),
    'JitTerrainProbe'], text=True)
expected_sum = float(re.search(r'TERRAIN_PROBE sum=([^ ]+)', reference)[1])
blobs = [('java', Path(os.environ.get('MINIJVM_CHECK_JAVA', str(ROOT/'src/build/minijvm/java'))).resolve()),
         ('rt', ROOT/'src/build/minijvm/minijvm_rt.jar'),
         ('probe', OUT/'probe.jar'), ('client', CLIENT)]
(OUT/'blobs.S').write_text('.section .rodata\n' + ''.join(
    f'.global {n}_start,{n}_end\n{n}_start: .incbin "{p}"\n{n}_end:\n'
    for n,p in blobs) + '.section .note.GNU-stack,"",@progbits\n')
cc = os.environ.get('CC', 'gcc')
headers = subprocess.check_output([cc, '-print-file-name=include'], text=True).strip()
run(cc, *(['-DJIT_PROFILE_CHECK'] if os.environ.get('MINIJVM_CHECK_PROFILE') == '1' else []), *(['-DJIT_METHOD_PROFILE_CHECK'] if os.environ.get('MINIJVM_CHECK_PROFILE_METHODS')=='1' else []), '-std=c11', '-O2', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
    '-fno-pic', '-fno-pie', '-m64', '-mno-red-zone', '-nostdinc', '-Isrc/include',
    '-Isrc/build/musl/sysroot/include', '-isystem', headers, '-c', TESTS/'JitMemoryProbe.c', '-o', OUT/'init.o')
run(cc, '-c', OUT/'blobs.S', '-o', OUT/'blobs.o')
run(cc, '-nostdlib', '-static', '-no-pie', '-Wl,-z,noexecstack', '-T', 'src/user/c_linker.ld',
    'src/build/musl/sysroot/lib/crt1.o', OUT/'init.o', OUT/'blobs.o', 'src/build/musl/sysroot/lib/libc.a', '-lgcc', '-o', OUT/'init.elf')
run('python3', 'src/tools/packers/mksilliconfs.py', OUT/'root.img', '--inodes', '1024',
    '--reserve-mib', '64', '--size-from', OUT/'init.elf')
run('src/build/populate_root', OUT/'root.img', OUT/'init.elf', 'src/build/shell.elf')
iso = OUT/'iso'
(iso/'EFI/BOOT').mkdir(parents=True, exist_ok=True)
(iso/'boot').mkdir(exist_ok=True)
for src, dst in [('src/limine/BOOTX64.EFI','EFI/BOOT/BOOTX64.EFI'),
                 ('src/limine/limine-uefi-cd.bin','boot/limine-uefi-cd.bin'),
                 ('src/build/kernel.elf','boot/kernel.elf'),
                 ('src/build/initramfs.cpio','boot/initramfs.cpio'),
                 (OUT/'root.img','boot/root.img')]:
    shutil.copyfile(src, iso/dst)
(iso/'limine.conf').write_text('timeout: 0\n/JIT check\n protocol: limine\n'
    ' path: boot():/boot/kernel.elf\n module_path: boot():/boot/initramfs.cpio\n'
    ' module_string: initramfs\n module_path: boot():/boot/root.img\n'
    ' module_string: live-root\n resolution: 1024x768x32\n')
run('xorriso', '-as', 'mkisofs', '-R', '-r', '-J', '--efi-boot', 'boot/limine-uefi-cd.bin',
    '-efi-boot-part', '--efi-boot-image', '--protective-msdos-label', iso, '-o', OUT/'check.iso')
log = OUT/'serial.log'
log.unlink(missing_ok=True)
qemu = subprocess.Popen(['qemu-system-x86_64', '-enable-kvm', '-cpu', 'host,+invtsc' if os.environ.get('MINIJVM_CHECK_PROFILE') == '1' else 'host', '-m', '1024',
    '-smp', '2', '-bios', '/usr/share/OVMF/OVMF_CODE.fd', '-cdrom', str(OUT/'check.iso'),
    '-display', 'none', '-serial', 'file:'+str(log), '-no-reboot'])
try:
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        text = log.read_text(errors='replace') if log.exists() else ''
        if 'JIT_CHECK_ALL_PASS' in text or '/sbin/init exited' in text or qemu.poll() is not None:
            break
        time.sleep(.5)
    print(text)
    if 'JIT_CHECK_ALL_PASS' not in text:
        raise SystemExit('JIT boot check failed: '+str(log))
    compiled = re.findall(r'JIT compiled methods: (\d+)', text)
    if len(compiled) != 2 or int(compiled[0]) == 0 or int(compiled[1]) != 0:
        raise SystemExit('JIT/interpreter modes were not both exercised')
    sums = re.findall(r'TERRAIN_PROBE sum=([^ ]+)', text)
    if os.environ.get('MINIJVM_CHECK_PROFILE') != '1' and (len(sums) != 2 or any(abs(float(value)-expected_sum) > .01 for value in sums)):
        raise SystemExit('JIT/interpreter terrain mismatch')
    if os.environ.get('MINIJVM_CHECK_PROFILE') == '1':
        profiles = [dict(re.findall(r'(\w+)=(\d+)', line)) for line in text.splitlines() if line.startswith('[MC profile]')]
        if len(profiles) < 2 or '[MC detail]' not in text or '[MC method]' not in text:
            raise SystemExit('Detailed profiler produced no reports')
        for row in profiles:
            total = sum(int(row[k]) for k in ['GL_ms','finish_ms','X11_ms','GC_ms','other_ms'])
            if not 0 <= int(row['interval_ms']) - total <= 5:
                raise SystemExit('Profiler categories overlap or lose time: '+str(row))
        print('PROFILE_EXCLUSIVE_ACCOUNTING_PASS')
finally:
    qemu.terminate()
    try: qemu.wait(timeout=5)
    except subprocess.TimeoutExpired: qemu.kill(); qemu.wait()
