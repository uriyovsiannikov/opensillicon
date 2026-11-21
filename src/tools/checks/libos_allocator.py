#!/usr/bin/env python3
# Exercise native allocation/reallocation, data retention, alignment and fork.
# Run from the repository root after `make live-iso` (requires KVM and OVMF).
from pathlib import Path
import os,shutil,subprocess,time
p=Path('src/build/allocator-check'); iso=p/'iso'
(iso/'EFI/BOOT').mkdir(parents=True,exist_ok=True);(iso/'boot').mkdir(exist_ok=True)
def run(*args):subprocess.run(list(map(str,args)),check=True,stdout=subprocess.DEVNULL)
headers=subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip()
run('gcc','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie','-m64','-mno-red-zone','-nostdinc','-Isrc/include','-Isrc/user/libos/include','-isystem',headers,'-c','src/tools/checks/libos_allocator.c','-o',p/'check.o')
run('gcc','-nostdlib','-static','-no-pie','-Wl,-z,noexecstack','-T','src/user/c_linker.ld','src/build/libos/crt0.o',p/'check.o',os.environ.get('LIBOS_ALLOCATOR_ARCHIVE','src/build/libos/liblibos.a'),'-lgcc','-o',p/'check.elf')
run('python3','src/tools/packers/mksilliconfs.py',p/'root.img','--inodes','1024','--reserve-mib','32','--size-from',p/'check.elf')
run('src/build/populate_root',p/'root.img',str(p/'check.elf'),'src/build/shell.elf')
for src,dst in [('src/limine/BOOTX64.EFI','EFI/BOOT/BOOTX64.EFI'),('src/limine/limine-uefi-cd.bin','boot/limine-uefi-cd.bin'),('src/build/kernel.elf','boot/kernel.elf'),('src/build/initramfs.cpio','boot/initramfs.cpio'),(p/'root.img','boot/root.img')]:shutil.copyfile(src,iso/dst)
(iso/'limine.conf').write_text('timeout: 0\n/Allocator check\n protocol: limine\n path: boot():/boot/kernel.elf\n module_path: boot():/boot/initramfs.cpio\n module_string: initramfs\n module_path: boot():/boot/root.img\n module_string: live-root\n resolution: 1024x768x32\n')
run('xorriso','-as','mkisofs','-R','-r','-J','--efi-boot','boot/limine-uefi-cd.bin','-efi-boot-part','--efi-boot-image','--protective-msdos-label',iso,'-o',p/'check.iso')
log=p/'serial.log';log.unlink(missing_ok=True)
q=subprocess.Popen(['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','1024','-bios','/usr/share/OVMF/OVMF_CODE.fd','-cdrom',str(p/'check.iso'),'-smp','2','-display','none','-serial','file:'+str(log),'-no-reboot'],stderr=subprocess.DEVNULL)
try:
 deadline=time.monotonic()+120
 while time.monotonic()<deadline:
  text=log.read_text(errors='replace') if log.exists() else ''
  if '/sbin/init exited' in text or q.poll() is not None:break
  time.sleep(.5)
 assert 'ALLOCATOR_PASS' in text and '/sbin/init exited with status 0' in text,text
 print(text,flush=True)
finally:
 q.terminate();q.wait(timeout=5)
