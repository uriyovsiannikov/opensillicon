#!/usr/bin/env python3
# Boot the actual audio driver and player with HDA, AC97 and no audio hardware.
# Run from the repository root after `make live-iso` (requires KVM and OVMF).
from pathlib import Path
import shutil,subprocess,time
p=Path('src/build/audio-pcm-check'); iso=p/'iso'
(iso/'EFI/BOOT').mkdir(parents=True,exist_ok=True);(iso/'boot').mkdir(exist_ok=True)
def run(*args):subprocess.run(list(map(str,args)),check=True,stdout=subprocess.DEVNULL)
headers=subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip()
run('gcc','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie','-m64','-mno-red-zone','-nostdinc','-Isrc/include','-Isrc/user/libos/include','-isystem',headers,'-c','src/tools/checks/audio_pcm.c','-o',p/'check.o')
run('gcc','-nostdlib','-static','-no-pie','-Wl,-z,noexecstack','-T','src/user/c_linker.ld','src/build/libos/crt0.o',p/'check.o','src/build/libos/liblibos.a','-lgcc','-o',p/'check.elf')
run('python3','src/tools/packers/mksilliconfs.py',p/'root.img','--inodes','1024','--reserve-mib','32','--size-from','src/build/audio-check/audiocodec.elf')
coreutils = next(line.split(':=',1)[1].split() for line in Path('src/mk/common.mk').read_text().splitlines() if line.startswith('COREUTILS :='))
run('src/build/populate_root',p/'root.img',str(p/'check.elf'),'src/build/shell.elf','--coreutils',*[f'src/build/coreutils/{name}.elf' for name in coreutils])
for src,dst in [('src/limine/BOOTX64.EFI','EFI/BOOT/BOOTX64.EFI'),('src/limine/limine-uefi-cd.bin','boot/limine-uefi-cd.bin'),('src/build/kernel.elf','boot/kernel.elf'),('src/build/initramfs.cpio','boot/initramfs.cpio'),(p/'root.img','boot/root.img')]:shutil.copyfile(src,iso/dst)
(iso/'limine.conf').write_text('timeout: 0\n/Audio diagnostic\n protocol: limine\n path: boot():/boot/kernel.elf\n module_path: boot():/boot/initramfs.cpio\n module_string: initramfs\n module_path: boot():/boot/root.img\n module_string: live-root\n resolution: 1024x768x32\n')
run('xorriso','-as','mkisofs','-R','-r','-J','--efi-boot','boot/limine-uefi-cd.bin','-efi-boot-part','--efi-boot-image','--protective-msdos-label',iso,'-o',p/'check.iso')
for mode,devices,expected in [('hda',['-device','intel-hda','-device','hda-duplex,audiodev=test'],'AUDIO_PCM_PASS'),('ac97',['-device','AC97,audiodev=test'],'AUDIO_PCM_SKIP_NO_AUDIO'),('none',[],'AUDIO_PCM_SKIP_NO_AUDIO')]:
 log=p/(mode+'-tone.log');log.unlink(missing_ok=True)
 q=subprocess.Popen(['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','512','-bios','/usr/share/OVMF/OVMF_CODE.fd','-cdrom',str(p/'check.iso'),'-smp','2','-display','none','-serial','file:'+str(log),'-audiodev','wav,id=test,path='+str(p/(mode+'-tone.wav'))+',out.frequency=48000,out.channels=2,out.format=s16',*devices,'-no-reboot'],stderr=subprocess.DEVNULL)
 try:
  deadline=time.monotonic()+60
  while time.monotonic()<deadline:
   text=log.read_text(errors='replace') if log.exists() else ''
   if '/sbin/init exited' in text or q.poll() is not None:break
   time.sleep(.5)
  assert expected in text and '/sbin/init exited with status 0' in text,text
  print(mode+': PASS',flush=True)
 finally:
  q.terminate();q.wait(timeout=5)
