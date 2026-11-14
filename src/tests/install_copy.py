import os,pathlib,stat,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as directory:
    base=pathlib.Path(directory);source=base/'source';target=base/'target'
    source.mkdir();target.mkdir()
    for name in ['bin','etc','etc/nested','home','home/user','dev','proc','init','mnt','tmp','var','var/run']:
        (source/name).mkdir()
    (source/'bin/program').write_bytes(bytes(range(256))*64);(source/'bin/program').chmod(0o755)
    (source/'etc/nested/config').write_text('preserve configuration\n');(source/'etc/nested/config').chmod(0o640)
    (source/'home/user/empty').touch();(source/'home/user').chmod(0o700)
    (source/'tmp').chmod(0o1777);(source/'tmp/img1.jpg').write_bytes(b'packaged background')
    for name in ['dev/transient','proc/uptime','init/source','mnt/recursive','var/run/socket','tmp/install.log']:
        (source/name).write_bytes(b'excluded runtime data')
    subprocess.run([sys.argv[1],str(source),str(target),'change'],check=True)
    for root,dirs,files in os.walk(target):
        for name in dirs+files:
            relative=(pathlib.Path(root)/name).relative_to(target)
            a=(source/relative).stat();b=(target/relative).stat()
            assert stat.S_IMODE(a.st_mode)==stat.S_IMODE(b.st_mode),(relative,a.st_mode,b.st_mode)
            assert (a.st_uid,a.st_gid)==(b.st_uid,b.st_gid)
    assert (target/'bin/program').read_bytes()==bytes(range(256))*64
    assert (target/'etc/nested/config').read_bytes()==(source/'etc/nested/config').read_bytes()
    assert (target/'tmp/img1.jpg').read_bytes()==b'packaged background'
    for name in ['dev/transient','proc/uptime','init/source','mnt/recursive','var/run/socket','tmp/install.log']:
        assert not (target/name).exists(),name
    (source/'etc/link').symlink_to('/etc/passwd')
    subprocess.run([sys.argv[1],str(source),str(target),'reject'],check=True)
print('installer copy: content, mode/owner, empty files, runtime exclusions and source-change detection: PASS')
