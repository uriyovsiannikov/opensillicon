import importlib.util, pathlib, subprocess, sys, tempfile
spec=importlib.util.spec_from_file_location('formatter','src/tools/packers/mksilliconfs.py')
formatter=importlib.util.module_from_spec(spec);spec.loader.exec_module(formatter)
with tempfile.TemporaryDirectory() as directory:
    root=pathlib.Path(directory)
    for blocks,inodes in [(16,32),(128,128),(32769,1024),(4096,32769)]:
        expected=root/'expected';actual=root/'actual'
        formatter.format_image(expected,blocks,inodes)
        with actual.open('wb') as image:image.truncate(blocks*4096)
        subprocess.run([sys.argv[1],str(actual),str(blocks),str(inodes),'0','0'],check=True)
        assert actual.read_bytes()==expected.read_bytes(),(blocks,inodes)
    for fail in range(1,10):
        with actual.open('wb') as image:image.truncate(128*4096)
        subprocess.run([sys.argv[1],str(actual),'128','128',str(fail),'0'],check=True)
    for fail in range(1,4):
        with actual.open('wb') as image:image.truncate(128*4096)
        subprocess.run([sys.argv[1],str(actual),'128','128','0',str(fail)],check=True)
print('newfs: byte comparison with Python formatter, bitmap boundaries, write/flush failures: PASS')
