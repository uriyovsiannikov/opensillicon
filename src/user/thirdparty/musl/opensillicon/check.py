#!/usr/bin/env python3
# Boot the standalone musl probe as init on OpenSillicon.
# Requires built kernel, initramfs, populate_root, shell, KVM and OVMF.
from pathlib import Path
import os,shutil,subprocess,time,socket,threading
p=Path('src/build/musl-check'); iso=p/'iso'
(iso/'EFI/BOOT').mkdir(parents=True,exist_ok=True);(iso/'boot').mkdir(exist_ok=True)
def run(*args):subprocess.run(list(map(str,args)),check=True,stdout=subprocess.DEVNULL)
# A hardware-independent module exercises the same file loader as boot drivers.
(p/'module_file_probe.c').write_text('int module_init(void) { return 0; }\nvoid module_fini(void) {}\n')
run(os.environ.get('CC','gcc'),'-std=c11','-O2','-ffreestanding','-fno-stack-protector',
    '-fno-pic','-fno-pie','-m64','-mno-red-zone','-mcmodel=kernel',
    '-fno-asynchronous-unwind-tables','-fno-unwind-tables','-c',
    p/'module_file_probe.c','-o',p/'module_file_probe.ko')
run('objcopy','--remove-section=.note.gnu.property',p/'module_file_probe.ko')
# Local echo peers exercise real guest IPv4 traffic through QEMU user networking.
tcp=socket.socket(); tcp.bind(('127.0.0.1',0)); tcp.listen(1); tcp.settimeout(20)
udp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); udp.bind(('127.0.0.1',0)); udp.settimeout(20)
peer_errors=[]
def tcp_echo():
 try:
  with tcp.accept()[0] as peer:
   peer.settimeout(10)
   message=b''
   while len(message)<5:
    chunk=peer.recv(5-len(message))
    if not chunk: raise RuntimeError('early TCP EOF')
    message+=chunk
   if message!=b'hello': raise RuntimeError(repr(message))
   peer.sendall(b'world')
 except Exception as error: peer_errors.append(str(error))
def udp_echo():
 try:
  message, address=udp.recvfrom(512)
  if message!=b'datagram': raise RuntimeError(repr(message))
  udp.sendto(message,address)
 except Exception as error: peer_errors.append(str(error))
threads=[threading.Thread(target=tcp_echo,daemon=True),threading.Thread(target=udp_echo,daemon=True)]
for thread in threads: thread.start()
run('src/build/musl/sysroot/bin/musl-gcc','-O2','-mno-red-zone','-Isrc/include','-Isrc/build/nano/ncurses/include','-static','-no-pie',
    '-DMUSL_TEST_TCP_PORT='+str(tcp.getsockname()[1]),'-DMUSL_TEST_UDP_PORT='+str(udp.getsockname()[1]),
    '-Wl,-T,src/user/thirdparty/musl/opensillicon/linker.ld','-Wl,--build-id=none',
    'src/user/thirdparty/musl/opensillicon/check.c','src/build/nano/ncurses/lib/libncurses.a','-o',p/'check.elf')
run('python3','src/tools/packers/mksilliconfs.py',p/'root.img','--inodes','1024','--reserve-mib','32','--size-from',p/'check.elf')
run('src/build/populate_root',p/'root.img',str(p/'check.elf'),'src/build/shell.elf','--module','src/build/modules/e1000.ko','--module',p/'module_file_probe.ko')
for src,dst in [('src/limine/BOOTX64.EFI','EFI/BOOT/BOOTX64.EFI'),('src/limine/limine-uefi-cd.bin','boot/limine-uefi-cd.bin'),(os.environ.get('MUSL_CHECK_KERNEL','src/build/kernel.elf'),'boot/kernel.elf'),('src/build/initramfs.cpio','boot/initramfs.cpio'),(p/'root.img','boot/root.img')]:shutil.copyfile(src,iso/dst)
(iso/'limine.conf').write_text('timeout: 0\n/musl check\n protocol: limine\n path: boot():/boot/kernel.elf\n module_path: boot():/boot/initramfs.cpio\n module_string: initramfs\n module_path: boot():/boot/root.img\n module_string: live-root\n resolution: 1024x768x32\n')
run('xorriso','-as','mkisofs','-R','-r','-J','--efi-boot','boot/limine-uefi-cd.bin','-efi-boot-part','--efi-boot-image','--protective-msdos-label',iso,'-o',p/'check.iso')
log=p/'serial.log';log.unlink(missing_ok=True)
q=subprocess.Popen(['qemu-system-x86_64','-accel','kvm','-cpu','host','-m','1024','-bios','/usr/share/OVMF/OVMF_CODE.fd','-cdrom',str(p/'check.iso'),'-netdev','user,id=musl-net','-device','e1000,netdev=musl-net','-smp','2','-display','none','-serial','file:'+str(log),'-no-reboot'],stderr=subprocess.DEVNULL)
try:
 deadline=time.monotonic()+120
 while time.monotonic()<deadline:
  text=log.read_text(errors='replace') if log.exists() else ''
  if '/sbin/init exited' in text or q.poll() is not None:break
  time.sleep(.5)
 assert 'MUSL_PASS' in text and '/sbin/init exited with status 0' in text,text
 for thread in threads: thread.join(timeout=1)
 assert not peer_errors,peer_errors
 assert not any(thread.is_alive() for thread in threads),'network peer unfinished'
 print(text,flush=True)
finally:
 q.terminate();q.wait(timeout=5)
 tcp.close();udp.close()
