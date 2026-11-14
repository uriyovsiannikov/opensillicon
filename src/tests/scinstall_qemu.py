"""Install a complete Live system onto a blank temporary SATA disk, then boot it.
No host disk is opened. Requires QEMU/KVM, OVMF, xorriso, sgdisk and mtools.
"""
import hashlib,json,os,pathlib,socket,struct,subprocess,tempfile,time
subprocess.run(['make','-j8','root','live-iso'],check=True)
def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def inventory(image,base=0):
    """Read persisted SilliconFS records independently of the guest VFS."""
    result={};seen=set()
    with image.open('rb') as stream:
        def read(offset,size):
            stream.seek(base+offset);data=stream.read(size);assert len(data)==size
            return data
        superblock=read(0,4096);assert superblock[:8]==b'SILFS1\0\0'
        table=struct.unpack_from('<I',superblock,56)[0]
        def walk(number,path):
            assert number not in seen;seen.add(number)
            inode=read(table*4096+(number-1)*128,128)
            mode,flags,uid,gid=struct.unpack_from('<HHII',inode);assert flags==0
            size=struct.unpack_from('<Q',inode,16)[0]
            pointers=list(struct.unpack_from('<12I',inode,24))
            indirect=struct.unpack_from('<I',inode,72)[0]
            if size>12*4096:
                assert indirect;pointers+=struct.unpack('<1024I',read(indirect*4096,4096))
            data=b''.join(read(block*4096,4096) if block else bytes(4096)
              for block in pointers[:(size+4095)//4096])[:size]
            isdir=(mode&0o170000)==0o040000
            result[path]=(mode,uid,gid,None if isdir else hashlib.sha256(data).hexdigest())
            if not isdir or path in ['/dev','/proc','/init','/mnt','/var/run']:return
            offset=0
            while offset<len(data):
                child,length,namesize,_=struct.unpack_from('<IHBB',data,offset)
                assert length>=8 and offset+length<=len(data) and namesize<=length-8
                name=data[offset+8:offset+8+namesize].decode();offset+=length
                if not child or name in ['.','..']:continue
                if path=='/tmp' and name!='img1.jpg':continue
                walk(child,path.rstrip('/')+'/'+name)
        walk(1,'/')
    return result
with tempfile.TemporaryDirectory(prefix='scinstall-') as directory:
    d=pathlib.Path(directory);disk=d/'target.img';guard=d/'guard.img'
    iso=d/'installer.iso';log=d/'serial';qmp=d/'qmp'
    for path,size in [(disk,1024*1024*1024),(guard,64*1024*1024)]:
        with path.open('wb') as stream:stream.truncate(size)
    with guard.open('r+b') as stream:stream.write(b'UNSELECTED DISK MUST REMAIN UNCHANGED\n')
    guard_hash=digest(guard);blank_hash=digest(disk)
    config=d/'limine.conf'
    subprocess.run(['xorriso','-osirrox','on','-indev','src/build/opensillicon-live.iso',
      '-extract','/limine.conf',str(config)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    config.chmod(0o600)
    menu=config.read_text()
    assert '/ OpenSillicon Live\n' in menu and 'cmdline: installer' in menu
    config.write_text('timeout: 0\n'+menu[menu.index('/ OpenSillicon Installer\n'):])
    subprocess.run(['xorriso','-indev','src/build/opensillicon-live.iso','-outdev',str(iso),
      '-map',str(config),'/limine.conf','-boot_image','any','replay'],check=True,
      stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    def start(live):
        log.unlink(missing_ok=True);qmp.unlink(missing_ok=True)
        args=[os.environ.get('QEMU','qemu-system-x86_64'),'-enable-kvm','-cpu','host','-machine','q35',
          '-m','1024M','-smp','2','-bios',os.environ.get('OVMF_CODE','/usr/share/OVMF/OVMF_CODE.fd'),
          '-display','none','-serial',f'file:{log}','-qmp',f'unix:{qmp},server=on,wait=off','-nic','none',
          '-drive',f'file={disk},if=none,id=target,format=raw',
          '-device','ide-hd,drive=target,bus=ide.0,model=Installer Target,serial=INSTALL001',
          '-drive',f'file={guard},if=none,id=guard,format=raw',
          '-device','ide-hd,drive=guard,bus=ide.1,serial=GUARD001']
        if live:args+=['-cdrom',str(iso)]
        return subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    def stop(p):
        if p.poll() is None:p.terminate();p.wait(timeout=5)
    def wait(p,marker,timeout=90):
        deadline=time.monotonic()+timeout;report=time.monotonic()+20
        while time.monotonic()<deadline:
            text=log.read_text(errors='replace') if log.exists() else ''
            if marker in text:return text
            if 'INSTALLATION FAILED' in text:raise RuntimeError(text[-6000:])
            if p.poll() is not None:raise RuntimeError(p.stderr.read().decode()+text[-6000:])
            if time.monotonic()>report:
                print('Waiting for',repr(marker),':',text[-250:],flush=True);report=time.monotonic()+20
            time.sleep(.1)
        raise RuntimeError('Missing '+marker+': '+text[-6000:])
    class Input:
        def __init__(self):
            self.socket=socket.socket(socket.AF_UNIX);self.socket.connect(str(qmp));self.file=self.socket.makefile('rwb')
            self.file.readline();self.command('qmp_capabilities')
        def command(self,cmd,args=None):
            self.file.write(json.dumps({'execute':cmd,'arguments':args or {}}).encode()+b'\n');self.file.flush()
            while True:
                result=json.loads(self.file.readline())
                if 'return' in result:return result['return']
                if 'error' in result:raise RuntimeError(result)
        def type(self,text):
            for char in text+'\n':
                key={' ':'spc','/':'slash','\n':'ret','.':'dot','-':'minus','_':'shift-minus'}.get(char,char)
                if char.isupper():key='shift-'+char.lower()
                self.command('human-monitor-command',{'command-line':'sendkey '+key});time.sleep(.12)
        def close(self):self.file.close();self.socket.close()
    # A wrong confirmation must leave every byte on both disks unchanged.
    p=start(True)
    try:
        wait(p,'Target disk (');keys=Input();keys.type('ada0')
        text=wait(p,'Confirmation:');assert 'INSTALL001' in text and '/dev/ada1' in text
        keys.type('cancel');wait(p,'Installation cancelled; no disk writes.')
        wait(p,'/% ');keys.type('scinstall -N /dev/ada0');wait(p,'root required')
        keys.close()
    finally:stop(p)
    assert digest(disk)==blank_hash and digest(guard)==guard_hash
    print('Cancelled installation and user-console restoration: PASS',flush=True)
    p=start(True)
    try:
        wait(p,'Target disk (');keys=Input();keys.type('ada0');wait(p,'Confirmation:')
        keys.type('ERASE /dev/ada0');wait(p,'INSTALLATION COMPLETE: /dev/ada0',600)
        wait(p,'Reboot now?');keys.type('n');wait(p,'/% ')
        keys.type('poweroff');p.wait(timeout=20);assert p.returncode==0;keys.close()
    finally:
        print(log.read_text(errors='replace')[-5000:],flush=True);stop(p)
    assert digest(guard)==guard_hash
    subprocess.run(['sgdisk','-v',str(disk)],check=True)
    with disk.open('rb') as stream:
        stream.seek(2*512);entries=stream.read(256)
        first,last=struct.unpack_from('<QQ',entries,32)
        root_uuid=entries[128+16:128+32]
        root_first=struct.unpack_from('<Q',entries,128+32)[0]
        stream.seek(first*512)
        esp=d/'esp.img'
        with esp.open('wb') as output:
            remaining=(last-first+1)*512
            while remaining:
                block=stream.read(min(1024*1024,remaining));assert block
                output.write(block);remaining-=len(block)
    subprocess.run(['fsck.fat','-n',str(esp)],check=True)
    for name,source in [('::/EFI/BOOT/BOOTX64.EFI','src/limine/BOOTX64.EFI'),
      ('::/boot/kernel.elf','src/build/kernel-boot.elf'),('::/boot/init.cpi','src/build/initramfs.cpio')]:
        out=d/'copied';subprocess.run(['mcopy','-o','-i',str(esp),name,str(out)],check=True)
        assert digest(out)==digest(pathlib.Path(source))
    source=inventory(pathlib.Path('src/build/live-root.img'))
    installed=inventory(disk,root_first*512)
    for name,record in source.items():
        assert installed.get(name)==record,('persisted content/ownership/mode mismatch',name)
    extras=set(installed)-set(source)
    assert extras<= {'/dev','/proc','/init','/mnt','/var/run','/var/log','/var/log/install.log'},extras
    for name in extras-{'/var/log/install.log'}:
        assert installed[name][0]&0o170000==0o040000 and installed[name][1:3]==(0,0),name
    assert '/var/log/install.log' in installed
    print('All persisted system files, ownership and modes match Live source: PASS',flush=True)
    import uuid
    out=d/'config';subprocess.run(['mcopy','-i',str(esp),'::/EFI/BOOT/limine.conf',str(out)],check=True)
    assert 'root=PARTUUID='+str(uuid.UUID(bytes_le=root_uuid)) in out.read_text()
    for boot in range(2):
        p=start(False)
        try:
            wait(p,'/% ');keys=Input()
            for name in ['/bin/scinstall','/bin/Xopensillicon','/boot/kernel.elf','/var/log/install.log']:
                keys.type('stat '+name);wait(p,'File: '+name)
            if boot==0:keys.type('touch /tmp/persist')
            keys.type('stat /tmp/persist');wait(p,'File: /tmp/persist')
            keys.type('poweroff');p.wait(timeout=20);assert p.returncode==0;keys.close()
        finally:stop(p)
    assert digest(guard)==guard_hash
print('Full native blank-disk installation, FAT/GPT verification, disk-only boots and persistence: PASS')
