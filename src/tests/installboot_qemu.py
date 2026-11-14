"""Boot files are installed by the guest; only the test's root FS is prepopulated."""
import importlib.util,json,os,pathlib,re,shutil,socket,subprocess,sys,tempfile,time
sys.dont_write_bytecode=True
subprocess.run(['make','-j8','root','live-iso','src/build/tests/installboot_guest.elf'],check=True)
coreutils=re.search(r'^COREUTILS := (.*)$',pathlib.Path('src/mk/common.mk').read_text(),re.M).group(1).split()
spec=importlib.util.spec_from_file_location('formatter','src/tools/packers/mksilliconfs.py')
formatter=importlib.util.module_from_spec(spec);spec.loader.exec_module(formatter)
with tempfile.TemporaryDirectory() as directory:
    d=pathlib.Path(directory);live=d/'live.img';disk=d/'disk.img';iso=d/'test.iso';log=d/'serial';qmp=d/'qmp'
    formatter.format_image(live,32768,1024)
    subprocess.run(['src/build/populate_root',str(live),'src/build/tests/installboot_guest.elf','src/build/shell.elf',
      '--boot-files','src/limine/BOOTX64.EFI','src/build/kernel-boot.elf','src/build/initramfs.cpio',
      '--coreutils',*[f'src/build/coreutils/{name}.elf' for name in coreutils]],check=True)
    with disk.open('wb') as stream:stream.truncate(256*1024*1024)
    subprocess.run(['sgdisk','-o','-n','1:2048:133119','-t','1:ef00','-n','2:133120:395263',
      '-t','2:53494c46-5300-4000-8000-4f50454e5349','-u','2:00112233-4455-4677-8899-aabbccddeeff',str(disk)],
      check=True,stdout=subprocess.DEVNULL)
    with disk.open('r+b') as target,open('src/build/root.img','rb') as source:
        target.seek(2048*512);target.write(bytes([0xcc])*512)
        target.seek(133120*512);shutil.copyfileobj(source,target)
    subprocess.run(['xorriso','-indev','src/build/opensillicon-live.iso','-outdev',str(iso),
      '-map',str(live),'/boot/root.img','-boot_image','any','replay'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    def start(cdrom):
        log.unlink(missing_ok=True);qmp.unlink(missing_ok=True)
        args=[os.environ.get('QEMU','qemu-system-x86_64'),'-enable-kvm','-machine','q35','-m','1024M','-smp','2',
          '-bios',os.environ.get('OVMF_CODE','/usr/share/OVMF/OVMF_CODE.fd'),'-display','none','-serial',f'file:{log}',
          '-qmp',f'unix:{qmp},server=on,wait=off','-nic','none','-drive',f'file={disk},if=none,id=disk,format=raw',
          '-device','ide-hd,drive=disk,bus=ide.0']
        if cdrom:args+=['-cdrom',str(iso)]
        return subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    def stop(p):
        if p.poll() is None:p.terminate();p.wait(timeout=5)
    def wait(p,marker,timeout=50):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            text=log.read_text(errors='replace') if log.exists() else ''
            if marker in text:return text
            if p.poll() is not None:raise RuntimeError(p.stderr.read().decode()+text[-4000:])
            time.sleep(.1)
        raise RuntimeError('Missing '+marker+': '+text[-5000:])
    p=start(True)
    try:wait(p,'INSTALLBOOT GUEST PASS:')
    finally:stop(p)
    esp=d/'esp.img'
    with disk.open('rb') as source,esp.open('wb') as target:
        source.seek(2048*512);target.write(source.read(64*1024*1024))
    subprocess.run(['fsck.fat','-n',str(esp)],check=True,stdout=subprocess.DEVNULL)
    for name,source in [('::/EFI/BOOT/BOOTX64.EFI','src/limine/BOOTX64.EFI'),('::/boot/kernel.elf','src/build/kernel-boot.elf'),('::/boot/init.cpi','src/build/initramfs.cpio')]:
        target=d/'copied';subprocess.run(['mcopy','-o','-i',str(esp),name,str(target)],check=True)
        assert target.read_bytes()==pathlib.Path(source).read_bytes()
    for reboot in range(2):
        p=start(False)
        try:
            wait(p,'/% ')
            c=socket.socket(socket.AF_UNIX);c.connect(str(qmp));f=c.makefile('rwb');f.readline()
            def command(cmd,args={}):
                f.write(json.dumps({'execute':cmd,'arguments':args}).encode()+b'\n');f.flush()
                while True:
                    result=json.loads(f.readline())
                    if 'return' in result:return
                    if 'error' in result:raise RuntimeError(result)
            command('qmp_capabilities')
            def typecmd(text):
                for char in text+'\n':
                    key={' ':'spc','/':'slash','\n':'ret'}.get(char,char)
                    command('human-monitor-command',{'command-line':'sendkey '+key});time.sleep(.09)
            if reboot==0:typecmd('touch /tmp/installed')
            typecmd('stat /tmp/installed');wait(p,'File: /tmp/installed')
            typecmd('poweroff');p.wait(timeout=15);assert p.returncode==0
            f.close();c.close()
        finally:stop(p)
print('Native installboot, independent FAT checks, disk-only UEFI boots and persistent root writes: PASS')
