import pathlib, subprocess, sys, tempfile
with tempfile.TemporaryDirectory() as directory:
    image=pathlib.Path(directory)/'disk.img'
    def run(sector,write=0,flush=0,corrupt=0):
        with image.open('wb') as stream:stream.truncate(192*1024*1024)
        subprocess.run([sys.argv[1],str(image),str(sector),str(write),str(flush),str(corrupt)],check=True)
    for sector in (512,4096):
        run(sector)
        if sector==512:
            report=subprocess.run(['sgdisk','--verify',str(image)],capture_output=True,text=True,check=True)
            assert 'No problems found' in report.stdout,report.stdout+report.stderr
        for write in range(1,6):run(sector,write=write)
        for flush in range(1,4):run(sector,flush=flush)
        for corrupt in range(1,12):run(sector,corrupt=corrupt)
print('GPT writer: kernel parser, sgdisk verification, 512/4096 sectors, write/flush/readback failures: PASS')
