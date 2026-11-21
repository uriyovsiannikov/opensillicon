#!/usr/bin/env python3
# Check exec environments, auxv, shell inheritance and curses startup in PTYs.
# Run from the repository root after `make live-iso` (requires KVM and OVMF).
from pathlib import Path
import os,shutil,subprocess,time
p=Path('src/build/environment-check'); iso=p/'iso'
(iso/'EFI/BOOT').mkdir(parents=True,exist_ok=True);(iso/'boot').mkdir(exist_ok=True)
def run(*args):subprocess.run(list(map(str,args)),check=True,stdout=subprocess.DEVNULL)
headers=subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip()
run('gcc',*(['-DLEGACY_KERNEL_CHECK'] if os.environ.get('ENVIRONMENT_CHECK_OLD_KERNEL') else []),'-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie','-m64','-mno-red-zone','-nostdinc','-Isrc/build/nano/ncurses/include','-Isrc/include','-Isrc/user/libos/include','-isystem',headers,'-c','src/tools/checks/libos_environment.c','-o',p/'check.o')
apps=['src/build/cmatrix/cmatrix','src/build/nsnake/nsnake','src/build/moon-buggy/moon-buggy','src/build/pipes.sh/pipes.sh','src/build/nano/nano','src/build/htop/htop']
(p/'blobs.S').write_text('.section .rodata\n'+''.join(f'.global app{i}_start,app{i}_end\napp{i}_start: .incbin "{f}"\napp{i}_end:\n' for i,f in enumerate(apps))+'.section .note.GNU-stack,"",@progbits\n')
run('gcc','-c',p/'blobs.S','-o',p/'blobs.o')
run('gcc','-nostdlib','-static','-no-pie','-Wl,-z,noexecstack','-T','src/user/c_linker.ld','src/build/libos/crt0.o',p/'check.o',p/'blobs.o','-Wl,--start-group','src/build/nano/ncurses/lib/libncurses.a','src/build/libos/liblibos.a','-Wl,--end-group','-lgcc','-o',p/'check.elf')
run('python3','src/tools/packers/mksilliconfs.py',p/'root.img','--inodes','1024','--reserve-mib','32','--size-from',p/'check.elf')
run('src/build/populate_root',p/'root.img',str(p/'check.elf'),'src/build/shell.elf')
for src,dst in [('src/limine/BOOTX64.EFI','EFI/BOOT/BOOTX64.EFI'),('src/limine/limine-uefi-cd.bin','boot/limine-uefi-cd.bin'),(os.environ.get('ENVIRONMENT_CHECK_OLD_KERNEL','src/build/kernel.elf'),'boot/kernel.elf'),('src/build/initramfs.cpio','boot/initramfs.cpio'),(p/'root.img','boot/root.img')]:shutil.copyfile(src,iso/dst)
(iso/'limine.conf').write_text('timeout: 0\n/Environment and terminal check\n protocol: limine\n path: boot():/boot/kernel.elf\n module_path: boot():/boot/initramfs.cpio\n module_string: initramfs\n module_path: boot():/boot/root.img\n module_string: live-root\n resolution: 1024x768x32\n')
run('xorriso','-as','mkisofs','-R','-r','-J','--efi-boot','boot/limine-uefi-cd.bin','-efi-boot-part','--efi-boot-image','--protective-msdos-label',iso,'-o',p/'check.iso')
log=p/'serial.log';log.unlink(missing_ok=True)
q=subprocess.Popen(['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','1024','-bios','/usr/share/OVMF/OVMF_CODE.fd','-cdrom',str(p/'check.iso'),'-smp','2','-display','none','-serial','file:'+str(log),'-no-reboot'],stderr=subprocess.DEVNULL)
try:
 deadline=time.monotonic()+120
 while time.monotonic()<deadline:
  text=log.read_text(errors='replace') if log.exists() else ''
  if '/sbin/init exited' in text or q.poll() is not None:break
  time.sleep(.5)
 assert 'ENVIRONMENT_PASS' in text and '/sbin/init exited with status 0' in text,text
 print(text,flush=True)
finally:
 q.terminate();q.wait(timeout=5)
