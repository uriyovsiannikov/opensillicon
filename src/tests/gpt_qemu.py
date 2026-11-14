# Run from the repository root after make iso root. Uses only temporary disks.
import shutil
import json,socket,subprocess,time,tempfile,pathlib
with tempfile.TemporaryDirectory() as d:
 pth=pathlib.Path(d); log=pth/'serial'; sock=pth/'qmp'
 for name,size in [('disk1',16),('disk2',32),('disk3',64)]:
  with (pth/name).open('wb') as image:image.truncate(size*1024*1024)
 with (pth/'disk2').open('wb') as image:image.truncate(192*1024*1024)
 subprocess.run(['sgdisk','-o','-n','1:2048:32767','-t','1:ef00','-n','3:32768:294911','-t','3:53494c46-5300-4000-8000-4f50454e5349',str(pth/'disk2')],check=True,stdout=subprocess.DEVNULL)
 with (pth/'disk2').open('r+b') as image,open('src/build/root.img','rb') as source:
  image.seek(32768*512);shutil.copyfileobj(source,image)
 p=subprocess.Popen(['qemu-system-x86_64','-enable-kvm','-machine','q35','-m','1024M','-smp','2','-bios','/usr/share/OVMF/OVMF_CODE.fd','-cdrom','src/build/opensillicon.iso','-display','none','-serial',f'file:{log}','-qmp',f'unix:{sock},server=on,wait=off','-nic','none','-drive',f'file={pth}/disk1,if=none,id=disk1,format=raw','-device','ide-hd,drive=disk1,bus=ide.0','-drive',f'file={pth}/disk2,if=none,id=disk2,format=raw','-device','ide-hd,drive=disk2,bus=ide.1','-device','ich9-ahci,id=extra','-drive',f'file={pth}/disk3,if=none,id=disk3,format=raw','-device','ide-hd,drive=disk3,bus=extra.0'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  for _ in range(150):
   if sock.exists():break
   time.sleep(.1)
  c=socket.socket(socket.AF_UNIX);c.connect(str(sock));f=c.makefile('rwb');f.readline()
  def command(cmd,args={}):
   f.write(json.dumps({'execute':cmd,'arguments':args}).encode()+b'\n');f.flush()
   while True:
    r=json.loads(f.readline())
    if 'return' in r:return r
    if 'error' in r:raise RuntimeError(r)
  command('qmp_capabilities')
  for _ in range(250):
   text=log.read_text(errors='replace') if log.exists() else ''
   if '/% ' in text:break
   if p.poll() is not None:raise RuntimeError(p.stderr.read().decode())
   time.sleep(.1)
  else:raise RuntimeError('No shell: '+text[-2000:])
  def typecmd(text):
   for ch in text+'\n':
    key={'\n':'ret',' ':'spc','/':'slash'}.get(ch,ch)
    command('human-monitor-command',{'command-line':'sendkey '+key})
    time.sleep(.09)
  typecmd('blkconf');time.sleep(.5)
  text=log.read_text(errors='replace')
  assert 'ram0' not in text,text[-2000:]
  assert 'USB input diagnostics' not in text
  for expected in ['ada0 131072 512 67108864','ada1 32768 512 16777216','ada2 393216 512 201326592']:assert expected in text,text[-2500:]
  assert 'ada2p3 262144 512 134217728' in text,text[-2500:]
  typecmd('stat /dev/ada2p3');time.sleep(.5)
  assert 'File: /dev/ada2p3' in log.read_text(errors='replace')
  typecmd('touch /tmp/probe');typecmd('stat /tmp/probe');time.sleep(.5)
  text=log.read_text(errors='replace')
  assert 'File: /tmp/probe' in text,text[-2500:]
  typecmd('poweroff')
  try:p.wait(timeout=12)
  except subprocess.TimeoutExpired:raise RuntimeError('Poweroff timed out: '+log.read_text(errors='replace')[-2000:])
  assert p.returncode==0,p.stderr.read().decode()
  print('PASS: GPT p3 root selected among three disks, partition devfs nodes, file creation, poweroff')
 finally:
  if p.poll() is None:p.terminate();p.wait(timeout=5)
