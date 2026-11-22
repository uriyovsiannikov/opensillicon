#!/usr/bin/env python3
"""Native random-access primitive I/O regression check. Optional MINIJVM_FILE_BASELINE_RT compares
an older runtime JAR using the same JVM binary and fixture. Requires KVM/OVMF.
Run from the repository root after building miniJVM, kernel and populate_root.
"""
from pathlib import Path
import os,subprocess,shutil,time,re
root=Path.cwd();out=root/'src/build/minijvm-file-check';out.mkdir(exist_ok=True)
def run(*a):subprocess.run(list(map(str,a)),check=True,stdout=subprocess.DEVNULL)
run('javac','--release','8','-d',out,'src/tools/pkg/repo/_src/minijvm/opensillicon/tests/JitFileProbe.java')
run('jar','cf',out/'probe.jar','-C',out,'JitFileProbe.class')
files=[('java','src/build/minijvm/java'),('oldrt',os.environ.get('MINIJVM_FILE_BASELINE_RT','src/build/minijvm/minijvm_rt.jar')),('rt','src/build/minijvm/minijvm_rt.jar'),('probe',str(out/'probe.jar'))]
(out/'blobs.S').write_text('.section .rodata\n'+''.join(f'.global {n}_start,{n}_end\n{n}_start: .incbin "{f}"\n{n}_end:\n' for n,f in files))
(out/'init.c').write_text('#define HAVE_BASELINE '+('1' if os.environ.get('MINIJVM_FILE_BASELINE_RT') else '0')+'\n'+'#include <stdio.h>\n#include <stdlib.h>\n#include <unistd.h>\n#include <sys/stat.h>\n#include <sys/wait.h>\nextern const unsigned char java_start[],java_end[],oldrt_start[],oldrt_end[],rt_start[],rt_end[],probe_start[],probe_end[];\nstatic void put(const char*n,const unsigned char*a,const unsigned char*b){FILE*f=fopen(n,"wb");if(!f||fwrite(a,1,b-a,f)!=(size_t)(b-a)||fclose(f))exit(1);}\nint main(void){put("/tmp/java",java_start,java_end);chmod("/tmp/java",0755);put("/tmp/old.jar",oldrt_start,oldrt_end);put("/tmp/rt.jar",rt_start,rt_end);put("/tmp/probe.jar",probe_start,probe_end);\nfor(int i=HAVE_BASELINE?0:1;i<2;i++){puts(i?"FILE_AFTER":"FILE_BEFORE");fflush(stdout);int p=fork();if(!p){char *a[]={"java","-Xmx256m","-bootclasspath",i?"/tmp/rt.jar":"/tmp/old.jar","-cp","/tmp/probe.jar","JitFileProbe",NULL};execv("/tmp/java",a);_exit(1);}int status;if(p<0||waitpid(p,&status,0)<0||status)return 2;}\nputs("FILE_COMPARE_PASS");fflush(stdout);return 0;}\n')
headers=subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip()
run('gcc','-O2','-ffreestanding','-fno-pie','-fno-builtin','-fno-stack-protector','-mno-red-zone','-nostdinc','-Isrc/include','-Isrc/build/musl/sysroot/include','-isystem',headers,'-c',out/'init.c','-o',out/'init.o')
run('gcc','-c',out/'blobs.S','-o',out/'blobs.o')
run('gcc','-nostdlib','-static','-no-pie','-Wl,-z,noexecstack','-T','src/user/c_linker.ld','src/build/musl/sysroot/lib/crt1.o',out/'init.o',out/'blobs.o','src/build/musl/sysroot/lib/libc.a','-lgcc','-o',out/'init.elf')
iso=out/'iso'
(iso/'EFI/BOOT').mkdir(parents=True,exist_ok=True);(iso/'boot').mkdir(exist_ok=True)
run('python3','src/tools/packers/mksilliconfs.py',out/'root.img','--inodes','1024','--reserve-mib','32','--size-from',out/'init.elf')
run('src/build/populate_root',out/'root.img',out/'init.elf','src/build/shell.elf')
for src,dst in [('src/limine/BOOTX64.EFI','EFI/BOOT/BOOTX64.EFI'),('src/limine/limine-uefi-cd.bin','boot/limine-uefi-cd.bin'),('src/build/kernel.elf','boot/kernel.elf'),('src/build/initramfs.cpio','boot/initramfs.cpio'),(out/'root.img','boot/root.img')]:shutil.copyfile(src,iso/dst)
(iso/'limine.conf').write_text('timeout: 0\n/File I/O check\n protocol: limine\n path: boot():/boot/kernel.elf\n module_path: boot():/boot/initramfs.cpio\n module_string: initramfs\n module_path: boot():/boot/root.img\n module_string: live-root\n resolution: 1024x768x32\n')
run('xorriso','-as','mkisofs','-R','-r','-J','--efi-boot','boot/limine-uefi-cd.bin','-efi-boot-part','--efi-boot-image','--protective-msdos-label',iso,'-o',out/'check.iso')
log=out/'serial.log';log.unlink(missing_ok=True)
q=subprocess.Popen(['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','1024','-smp','2','-bios','/usr/share/OVMF/OVMF_CODE.fd','-cdrom',str(out/'check.iso'),'-display','none','-serial','file:'+str(log),'-no-reboot'],stderr=subprocess.DEVNULL)
try:
 deadline=time.monotonic()+180
 while time.monotonic()<deadline:
  t=log.read_text(errors='replace') if log.exists() else ''
  if 'FILE_COMPARE_PASS' in t or '/sbin/init exited' in t:break
  time.sleep(.5)
 print(t)
 assert 'FILE_COMPARE_PASS' in t
 checks=re.findall(r'FILE_PROBE_PASS elapsed_ms=(\d+)',t);assert len(checks)==(2 if os.environ.get('MINIJVM_FILE_BASELINE_RT') else 1)
 print('elapsed_ms='+checks[-1])
finally:q.terminate();q.wait(timeout=5)
