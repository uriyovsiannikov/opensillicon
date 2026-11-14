import pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as directory:
    root=pathlib.Path(directory);image=root/'esp.img';config=root/'limine.conf'
    config.write_text('timeout: 0\n/OpenSillicon\n protocol: limine\n path: boot():/boot/kernel.elf\n module_path: boot():/boot/init.cpi\n module_string: initramfs\n resolution: 1920x1080x32\n')
    files=['src/limine/BOOTX64.EFI',str(config),'src/build/kernel.elf','src/build/initramfs.cpio']
    names=['::/EFI/BOOT/BOOTX64.EFI','::/EFI/BOOT/limine.conf','::/boot/kernel.elf','::/boot/init.cpi']
    for sector,size in [(512,64),(4096,512)]:
        with image.open('wb') as stream:stream.truncate(size*1024*1024)
        subprocess.run([sys.argv[1],str(image),str(sector),*files,'0','0'],check=True)
        report=subprocess.run(['fsck.fat','-n','-v',str(image)],text=True,capture_output=True)
        assert report.returncode==0,report.stdout+report.stderr
        for source,name in zip(files,names):
            target=root/'copy'
            subprocess.run(['mcopy','-o','-i',str(image),name,str(target)],check=True)
            assert target.read_bytes()==pathlib.Path(source).read_bytes(),name
    for write,flush in [(1,0),(2,0),(20,0),(34,0),(40,0),(0,1),(0,2),(0,3),(0,4)]:
        with image.open('wb') as stream:stream.truncate(64*1024*1024)
        subprocess.run([sys.argv[1],str(image),'512',*files,str(write),str(flush)],check=True)
print('ESP FAT32: fsck.fat, mcopy, 512/4096 sectors, file contents and write/flush failures: PASS')
