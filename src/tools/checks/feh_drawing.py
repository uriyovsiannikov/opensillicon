#!/usr/bin/env python3
"""Check feh text layout, caption paths and offscreen drawing with native Imlib2."""
from pathlib import Path
import shutil
import subprocess
import time

out = Path('src/build/feh-drawing-check').resolve()
out.mkdir(parents=True, exist_ok=True)

def run(*args):
    subprocess.run(list(map(str, args)), check=True, stdout=subprocess.DEVNULL)

run('make', '-j6', 'kernel', 'src/build/populate_root', 'src/build/shell.elf', 'src/build/initramfs.cpio')
prefix = Path('src/build/x11/prefix').resolve()
source = Path('src/tools/pkg/repo/_src/twm/feh/src').resolve()
objects_dir = Path('src/build/x11/feh/src').resolve()
objects = subprocess.check_output(['make', '--no-print-directory', '-s', '-C', str(objects_dir),
    '--eval=print-objects:;@echo $(OBJECTS)', 'print-objects'], text=True).split()
run('objcopy', '--redefine-sym', 'main=feh_program_main', objects_dir / 'main.o', out / 'feh-main.o')
objects = [out / 'feh-main.o' if obj == 'main.o' else objects_dir / obj for obj in objects]
font = Path('src/tools/pkg/repo/_src/twm/feh/share/fonts/NotoSans-Medium.ttf').resolve()
(out / 'font.S').write_text('.section .rodata\n.global font_start,font_end\nfont_start:\n'
    f'.incbin "{font}"\nfont_end:\n.section .note.GNU-stack,"",@progbits\n')
run('src/build/musl/sysroot/bin/musl-gcc', '-std=c11', '-O2', '-mno-red-zone', '-static', '-no-pie',
    '-Wl,-T,src/user/c_linker.ld', '-Wl,--build-id=none', '-I' + str(source), '-I' + str(prefix / 'include'),
    '-idirafter', '/usr/include',
    'src/tools/checks/feh_drawing.c', out / 'font.S', *objects, '-L' + str(prefix / 'lib'),
    '-Wl,--start-group', prefix / 'lib/libi18n.a', '-lImlib2', '-lfreetype', '-lpng', '-ljpeg',
    'src/build/libz/libz.a', '-lXext', '-lX11', '-lxcb', '-lXau',
    'src/build/musl/sysroot/lib/libc.a', '-Wl,--end-group', '-lgcc', '-o', out / 'init.elf')
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
(iso / 'limine.conf').write_text('timeout: 0\n/feh drawing check\n protocol: limine\n'
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
    assert 'FEH_DRAWING_PASS' in text and '/sbin/init exited with status 0' in text, text
    print('FEH_DRAWING_PASS: native text wrapping, caption paths and offscreen text backgrounds; X window drawing not tested')
finally:
    guest.terminate()
    guest.wait(timeout=5)
