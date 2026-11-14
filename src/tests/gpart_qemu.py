"""Run from repository root. All formatting targets temporary images."""
import sys
sys.dont_write_bytecode=True
import importlib.util, os, pathlib, re, subprocess, tempfile, time
root=pathlib.Path.cwd()
subprocess.run(['make','-j8','live-iso','src/build/tests/gpart_guest.elf','src/build/populate_root'],check=True)
coreutils=re.search(r'^COREUTILS := (.*)$',(root/'src/mk/common.mk').read_text(),re.M).group(1).split()
spec=importlib.util.spec_from_file_location('formatter',root/'src/tools/packers/mksilliconfs.py')
formatter=importlib.util.module_from_spec(spec);spec.loader.exec_module(formatter)
with tempfile.TemporaryDirectory() as directory:
    directory=pathlib.Path(directory);image=directory/'root.img';disk=directory/'disk.img'
    iso=directory/'test.iso';log=directory/'serial.log'
    formatter.format_image(image,32768,1024)
    subprocess.run(['src/build/populate_root',str(image),'src/build/tests/gpart_guest.elf',
                    'src/build/shell.elf','--coreutils',*[f'src/build/coreutils/{name}.elf' for name in coreutils]],check=True)
    with disk.open('wb') as stream:
        stream.truncate(192*1024*1024);stream.write(bytes([0xcc])*512)
    subprocess.run(['xorriso','-indev','src/build/opensillicon-live.iso','-outdev',str(iso),
                    '-map',str(image),'/boot/root.img','-boot_image','any','replay'],
                   check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    for phase in (1,2):
        process=subprocess.Popen([os.environ.get('QEMU','qemu-system-x86_64'),'-enable-kvm','-cpu','host','-machine','q35',
          '-m','1024M','-smp','2','-bios',os.environ.get('OVMF_CODE','/usr/share/OVMF/OVMF_CODE.fd'),
          '-cdrom',str(iso),'-display','none','-serial',f'file:{log}','-nic','none',
          '-drive',f'file={disk},if=none,id=testdisk,format=raw','-device','ide-hd,drive=testdisk,bus=ide.0'],
          stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            deadline=time.monotonic()+40
            while time.monotonic()<deadline:
                text=log.read_text(errors='replace') if log.exists() else ''
                if f'GPART PHASE{phase} PASS:' in text:break
                if process.poll() is not None:raise RuntimeError(process.stderr.read().decode()+text[-5000:])
                time.sleep(.1)
            else:raise RuntimeError('Guest test did not finish: '+text[-5000:])
        finally:
            if process.poll() is None:process.terminate();process.wait(timeout=5)
        if phase==1:
            check=subprocess.run(['sgdisk','--verify',str(disk)],capture_output=True,text=True,check=True)
            assert 'No problems found' in check.stdout,check.stdout+check.stderr
    import struct
    with disk.open('rb') as stream:
        stream.seek(2*512+128+32)
        first,last=struct.unpack('<QQ',stream.read(16))
        stream.seek(first*512);actual=stream.read((last-first+1)*512)
    expected=directory/'expected.img';formatter.format_image(expected,len(actual)//4096,128)
    assert actual==expected.read_bytes()
print('Native gpart, sgdisk validation, reboot, GPT device discovery and native newfs: PASS')
