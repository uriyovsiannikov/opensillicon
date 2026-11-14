"""Run from repository root. All formatting targets temporary images."""
import importlib.util, os, pathlib, re, subprocess, tempfile, time
root=pathlib.Path.cwd()
subprocess.run(['make','-j8','live-iso','src/build/tests/newfs_guest.elf','src/build/populate_root'],check=True)
coreutils=re.search(r'^COREUTILS := (.*)$',(root/'src/mk/common.mk').read_text(),re.M).group(1).split()
spec=importlib.util.spec_from_file_location('formatter',root/'src/tools/packers/mksilliconfs.py')
formatter=importlib.util.module_from_spec(spec);spec.loader.exec_module(formatter)
with tempfile.TemporaryDirectory() as directory:
    directory=pathlib.Path(directory);image=directory/'root.img';disk=directory/'disk.img'
    iso=directory/'test.iso';log=directory/'serial.log'
    formatter.format_image(image,32768,1024)
    subprocess.run(['src/build/populate_root',str(image),'src/build/tests/newfs_guest.elf',
                    'src/build/shell.elf','--coreutils',*[f'src/build/coreutils/{name}.elf' for name in coreutils]],check=True)
    with disk.open('wb') as stream:
        stream.truncate(16*1024*1024);stream.write(bytes([0xcc])*512)
    subprocess.run(['xorriso','-indev','src/build/opensillicon-live.iso','-outdev',str(iso),
                    '-map',str(image),'/boot/root.img','-boot_image','any','replay'],
                   check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    process=subprocess.Popen([os.environ.get('QEMU','qemu-system-x86_64'),'-enable-kvm','-machine','q35',
      '-m','1024M','-smp','2','-bios',os.environ.get('OVMF_CODE','/usr/share/OVMF/OVMF_CODE.fd'),
      '-cdrom',str(iso),'-display','none','-serial',f'file:{log}','-nic','none',
      '-drive',f'file={disk},if=none,id=testdisk,format=raw','-device','ide-hd,drive=testdisk,bus=ide.0'],
      stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        deadline=time.monotonic()+40
        while time.monotonic()<deadline:
            text=log.read_text(errors='replace') if log.exists() else ''
            if 'NEWFS GUEST PASS:' in text:break
            if process.poll() is not None:raise RuntimeError(process.stderr.read().decode()+text[-5000:])
            time.sleep(.1)
        else:raise RuntimeError('Guest test did not finish: '+text[-5000:])
    finally:
        if process.poll() is None:process.terminate();process.wait(timeout=5)
    expected=directory/'expected.img';formatter.format_image(expected,4096,128)
    assert disk.read_bytes()==expected.read_bytes()
print('Native newfs_silliconfs in QEMU, device ioctls and independent image comparison: PASS')
