#!/usr/bin/env python3
"""Boot the built twm and check startup, version and invalid-option paths."""
from pathlib import Path
import shutil
import subprocess
import time

out = Path('src/build/twm-check').resolve()
out.mkdir(parents=True, exist_ok=True)

def run(*args):
    subprocess.run(list(map(str, args)), check=True, stdout=subprocess.DEVNULL)

run('meson', 'compile', '-C', 'src/build/x11/twm')
run('make', '-j6', 'kernel', 'src/build/populate_root', 'src/build/shell.elf', 'src/build/initramfs.cpio')
binary = Path('src/build/x11/twm/src/twm').resolve()
(out / 'blob.S').write_text('.section .rodata\n.global game_start,game_end\ngame_start:\n'
                           f'.incbin "{binary}"\ngame_end:\n.section .note.GNU-stack,"",@progbits\n')
(out / 'init.c').write_text(r'''
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
extern const unsigned char game_start[], game_end[];
int main(void) {
 FILE *file=fopen("/tmp/twm","wb");
 size_t length=(size_t)(game_end-game_start);
 if(!file) return 1;
 if(fwrite(game_start,1,length,file)!=length) return 1;
 if(fclose(file) || chmod("/tmp/twm",0755)) return 1;
 const char *options[]={"-V","-invalid"};
 for(int i=0;i<2;i++) {
  pid_t child=fork(); if(child<0) return 2;
  if(!child) { char *args[]={"twm",(char *)options[i],NULL}; execv("/tmp/twm",args); _exit(3); }
  int status; if(waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=i) return 4;
 }
 puts("TWM_STARTUP_PASS"); return 0;
}
''')
run('src/build/musl/sysroot/bin/musl-gcc', '-O2', '-mno-red-zone', '-static', '-no-pie',
    '-Wl,-T,src/user/c_linker.ld', '-Wl,--build-id=none', out / 'init.c', out / 'blob.S', '-o', out / 'init.elf')
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
(iso / 'limine.conf').write_text('timeout: 0\n/twm check\n protocol: limine\n'
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
    assert 'TWM_STARTUP_PASS' in text and '/sbin/init exited with status 0' in text, text
    assert 'usage:  twm' in text and '1.0.13.1' in text, text
    print('TWM_STARTUP_PASS: native version and invalid-option startup; window management not tested')
finally:
    guest.terminate()
    guest.wait(timeout=5)
