#!/usr/bin/env python3
"""Boot the real XMMS/GTK GUI with TinyX and HDA; verify WAV/MP3 and controls.
Requires an existing native X11 build, kernel, initramfs, KVM, ffmpeg and OVMF.
Run from the repository root after make -C src/tools/pkg/repo/_src/xmms.
"""
from pathlib import Path
import shutil,subprocess,time,socket,wave,math,struct
from array import array
p=Path('src/build/xmms/check');p.mkdir(parents=True,exist_ok=True)
def run(*args):subprocess.run(list(map(str,args)),check=True,stdout=subprocess.DEVNULL)
for name,hz in [('test.wav',440),('mp3-input.wav',880)]:
 with wave.open(str(p/name),'wb') as w:
  w.setparams((2,2,44100,0,'NONE','not compressed'))
  w.writeframes(b''.join(struct.pack('<hh',*[int(4000*math.sin(2*math.pi*hz*i/44100))]*2) for i in range(44100*8)))
run('ffmpeg','-v','error','-y','-i',p/'mp3-input.wav','-codec:a','libmp3lame','-b:a','128k',p/'test.mp3')
files=[('src/build/xmms/stage/usr/bin/xmms','/usr/bin/xmms'),('src/build/pkg/xorg-stage/usr/bin/X','/usr/bin/X'),(str(p/'test.wav'),'/tmp/test.wav'),(str(p/'test.mp3'),'/tmp/test.mp3')]
fonts=Path('src/build/pkg/xorg-stage/usr/share/fonts')
files += [(str(f),'/usr/share/fonts/'+str(f.relative_to(fonts))) for f in sorted(fonts.rglob('*')) if f.is_file()]
(p/'blobs.S').write_text('.section .rodata\n'+''.join(f'.global blob{i}_start,blob{i}_end\nblob{i}_start: .incbin "{f}"\nblob{i}_end:\n' for i,(f,_) in enumerate(files)))
(p/'blobs.h').write_text(''.join(f'extern const unsigned char blob{i}_start[],blob{i}_end[];\n' for i in range(len(files)))+'static const struct {const char *name; const unsigned char *start,*end;} payload[]={\n'+''.join(f'{{"{name}",blob{i}_start,blob{i}_end}},\n' for i,(_,name) in enumerate(files))+'};\n')
headers=subprocess.check_output(['gcc','-print-file-name=include'],text=True).strip()
run('gcc','-O2','-ffreestanding','-fno-builtin','-fno-pie','-fno-stack-protector','-mno-red-zone','-nostdinc','-Isrc/user/libos/include','-Isrc/include','-I'+str(p),'-isystem',headers,'-c','src/tools/checks/xmms.c','-o',p/'init.o')
run('gcc','-fno-pie','-c',p/'blobs.S','-o',p/'blobs.o')
run('gcc','-nostdlib','-static','-no-pie','-Wl,-z,noexecstack','-T','src/user/c_linker.ld','src/build/libos/crt0.o',p/'init.o',p/'blobs.o','src/build/xmms/compat.o','-Wl,--start-group','src/build/xmms/player/libxmms/.libs/libxmms.a','src/build/xmms/prefix/lib/libglib.a','src/build/libos/liblibos.a','-lgcc','-Wl,--end-group','-o',p/'init.elf')
(p/'screen.ppm').unlink(missing_ok=True)
p=Path('src/build/xmms/check'); iso=p/'iso'
(iso/'EFI/BOOT').mkdir(parents=True,exist_ok=True);(iso/'boot').mkdir(exist_ok=True)
run('python3','src/tools/packers/mksilliconfs.py',p/'root.img','--inodes','1024','--reserve-mib','32','--size-from',str(p/'init.elf'))
run('src/build/populate_root',p/'root.img',str(p/'init.elf'),'src/build/shell.elf')
for src,dst in [('src/limine/BOOTX64.EFI','EFI/BOOT/BOOTX64.EFI'),('src/limine/limine-uefi-cd.bin','boot/limine-uefi-cd.bin'),('src/build/kernel.elf','boot/kernel.elf'),('src/build/initramfs.cpio','boot/initramfs.cpio'),(p/'root.img','boot/root.img')]:shutil.copyfile(src,iso/dst)
(iso/'limine.conf').write_text('timeout: 0\n/Audio diagnostic\n protocol: limine\n path: boot():/boot/kernel.elf\n module_path: boot():/boot/initramfs.cpio\n module_string: initramfs\n module_path: boot():/boot/root.img\n module_string: live-root\n resolution: 1024x768x32\n')
run('xorriso','-as','mkisofs','-R','-r','-J','--efi-boot','boot/limine-uefi-cd.bin','-efi-boot-part','--efi-boot-image','--protective-msdos-label',iso,'-o',p/'check.iso')
for mode,devices,expected in [('hda',['-device','intel-hda','-device','hda-duplex,audiodev=test'],'XMMS_CHECK_PASS')]:
 log=p/(mode+'.log');log.unlink(missing_ok=True)
 q=subprocess.Popen(['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','512','-bios','/usr/share/OVMF/OVMF_CODE.fd','-cdrom',str(p/'check.iso'),'-display','none','-monitor','unix:'+str(p.resolve()/'monitor')+',server,nowait','-serial','file:'+str(log),'-audiodev','wav,id=test,path='+str(p/'capture.wav'),*devices,'-no-reboot'],stderr=subprocess.DEVNULL)
 try:
  deadline=time.monotonic()+150
  while time.monotonic()<deadline:
   text=log.read_text(errors='replace') if log.exists() else ''
   if 'XMMS_WAV_PLAYING' in text and not (p/'screen.ppm').exists():
    s=socket.socket(socket.AF_UNIX);s.connect(str(p.resolve()/'monitor'));s.recv(4096);s.sendall(('screendump '+str(p.resolve()/'screen.ppm')+'\n').encode());time.sleep(.1);s.close()
   if expected in text or 'XMMS_FAIL' in text or q.poll() is not None:break
   time.sleep(.5)
  assert expected in text and 'XMMS_FAIL' not in text,text
  print(mode+': PASS',flush=True)
 finally:
  # The wav backend patches RIFF/data lengths during an orderly quit.
  try:
   s=socket.socket(socket.AF_UNIX);s.connect(str(p.resolve()/'monitor'));s.recv(4096);s.sendall(b'quit\n');s.close();q.wait(timeout=5)
  except (OSError,subprocess.TimeoutExpired):
   q.terminate();q.wait(timeout=5)

# Some QEMU wav backends leave the two streaming length fields zero.
# Validate the fixed PCM header, then finalize this test artifact ourselves.
recording=p/'capture.wav'
b=bytearray(recording.read_bytes())
assert b[:4]==b'RIFF' and b[8:12]==b'WAVE' and b[12:16]==b'fmt ' and b[36:40]==b'data'
assert struct.unpack_from('<HH',b,20)==(1,2) and struct.unpack_from('<H',b,34)[0]==16
assert len(b)>44 and (len(b)-44)%4==0
struct.pack_into('<I',b,4,len(b)-8);struct.pack_into('<I',b,40,len(b)-44)
recording.write_bytes(b)
with wave.open(str(recording),'rb') as w:
 rate=w.getframerate();channels=w.getnchannels();samples=array('h',w.readframes(w.getnframes()))[::channels]
found=[]
for start in range(0,len(samples)-rate//4,rate//4):
 chunk=samples[start:start+rate//4]
 if max(map(abs,chunk),default=0)<100:continue
 crossings=sum(a<=0<b for a,b in zip(chunk,chunk[1:]))*4
 found.append(crossings)
assert any(abs(f-440)<20 for f in found),('missing WAV tone',found)
assert any(abs(f-880)<20 for f in found),('missing MP3 tone',found)
print('Captured 440 Hz WAV and 880 Hz MP3: PASS')
print('Window screenshot: '+str(p/'screen.ppm'))
