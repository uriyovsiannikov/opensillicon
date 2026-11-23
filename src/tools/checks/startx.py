#!/usr/bin/env python3
"""Boot the packaged startx session, find XTerm, and verify signal cleanup."""
from pathlib import Path
import shutil
import subprocess
import time

out = Path('src/build/startx-local-check').resolve()
out.mkdir(parents=True, exist_ok=True)

def run(*args):
    subprocess.run(list(map(str, args)), check=True, stdout=subprocess.DEVNULL)

run('make','-j6','kernel','src/build/populate_root','src/build/shell.elf','src/build/initramfs.cpio')
run('make','-j6','-C','src/tools/pkg/repo/_src/twm')
prefix=Path('src/build/x11/prefix').resolve()
stage=Path('src/build/pkg/xorg-stage')
files=[(f,'/'+str(f.relative_to(stage))) for f in sorted(stage.rglob('*')) if f.is_file()]
(out/'blobs.S').write_text('.section .rodata\n'+''.join(f'.global blob{i}_start,blob{i}_end\nblob{i}_start: .incbin "{f.resolve()}"\nblob{i}_end:\n' for i,(f,_) in enumerate(files))+'\n.section .note.GNU-stack,"",@progbits\n')
(out/'blobs.h').write_text(''.join(f'extern const unsigned char blob{i}_start[],blob{i}_end[];\n' for i in range(len(files)))+'static const struct {const char *name;const unsigned char *start,*end;} payload[]={\n'+''.join(f'{{"{name}",blob{i}_start,blob{i}_end}},\n' for i,(_,name) in enumerate(files))+'};\n')
run('src/build/musl/sysroot/bin/musl-gcc','-O2','-mno-red-zone','-static','-no-pie','-Isrc/include','-I'+str(out),'-I'+str(prefix/'include'),'-idirafter','/usr/include','-Wl,-T,src/user/c_linker.ld','-Wl,--build-id=none','src/tools/checks/startx.c',out/'blobs.S','-L'+str(prefix/'lib'),'-Wl,--start-group',prefix/'lib/libi18n.a','-lX11','-lxcb','-lXau','src/build/musl/sysroot/lib/libc.a','-Wl,--end-group','-lgcc','-o',out/'init.elf')
run('python3', 'src/tools/packers/mksilliconfs.py', out / 'root.img', '--inodes', '1024',
    '--reserve-mib', '32', '--size-from', out / 'init.elf')
run('src/build/populate_root', out / 'root.img', out / 'init.elf', 'src/build/shell.elf')
iso = out / 'iso'
(iso / 'EFI/BOOT').mkdir(parents=True, exist_ok=True)
(iso / 'boot').mkdir(exist_ok=True)
for source, target in [('src/limine/BOOTX64.EFI', 'EFI/BOOT/BOOTX64.EFI'),
                       ('src/limine/limine-uefi-cd.bin', 'boot/limine-uefi-cd.bin'),
                       ('src/build/kernel.elf', 'boot/kernel.elf'),
                       ('src/build/initramfs.cpio', 'boot/initramfs.cpio'),
                       (out / 'root.img', 'boot/root.img')]:
    shutil.copyfile(source, iso / target)
(iso / 'limine.conf').write_text('timeout: 0\n/X11 local check\n protocol: limine\n'
    ' path: boot():/boot/kernel.elf\n module_path: boot():/boot/initramfs.cpio\n'
    ' module_string: initramfs\n module_path: boot():/boot/root.img\n'
    ' module_string: live-root\n resolution: 1024x768x32\n')
run('xorriso', '-as', 'mkisofs', '-R', '-r', '-J', '--efi-boot', 'boot/limine-uefi-cd.bin',
    '-efi-boot-part', '--efi-boot-image', '--protective-msdos-label', iso, '-o', out / 'check.iso')
log = out / 'serial.log'
log.unlink(missing_ok=True)
guest = subprocess.Popen(['qemu-system-x86_64', '-accel', 'kvm', '-cpu', 'host', '-m', '512',
    '-smp', '2', '-bios', '/usr/share/OVMF/OVMF_CODE.fd', '-cdrom', str(out / 'check.iso'),
    '-display', 'none', '-serial', 'file:' + str(log), '-no-reboot'], stderr=subprocess.PIPE)
try:
    deadline = time.monotonic() + 60
    text = ''
    while time.monotonic() < deadline:
        text = log.read_text(errors='replace') if log.exists() else ''
        if '/sbin/init exited' in text or guest.poll() is not None:
            break
        time.sleep(.2)
    assert 'STARTX_PASS' in text and '/sbin/init exited with status 0' in text, text
    print('STARTX_PASS: desktop windows and session signal cleanup')
finally:
    guest.terminate()
    guest.wait(timeout=5)
