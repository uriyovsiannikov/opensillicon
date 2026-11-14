import pathlib, struct, subprocess, sys, tempfile, uuid, zlib
binary=sys.argv[1]
pack=lambda fmt,*values:struct.pack('<'+fmt,*values)

def image(sector,kind):
    count=4096
    a=bytearray(16384)
    for slot,start,end in [(0,128,255),(2,512,767)]:
        off=slot*128
        a[off:off+16]=uuid.UUID('0fc63daf-8483-4772-8e79-3d69d8477de4').bytes_le
        a[off+16:off+32]=uuid.UUID(int=slot+1).bytes_le
        a[off+32:off+48]=pack('QQ',start,end)
    if kind=='overlap': a[288:304]=pack('QQ',200,300)
    if kind=='outside': a[32:48]=pack('QQ',0,255)
    if kind=='reversed': a[32:48]=pack('QQ',255,128)
    if kind=='duplicate': a[272:288]=a[16:32]
    if kind=='null-uuid': a[16:32]=bytes(16)
    sectors=len(a)//sector
    disk=bytearray(count*sector)
    disk[510:512]=b'\x55\xaa'
    disk[450]=0xee
    disk[454:462]=pack('II',1,count-1)
    if kind=='hybrid': disk[466]=0x83
    if kind=='absent': disk[450]=0
    for backup in (False,True):
        array=bytearray(a)
        if backup and kind=='conflict': array[32:48]=pack('QQ',129,255)
        lba=count-1 if backup else 1
        table_lba=count-1-sectors if backup else 2
        h=bytearray(sector)
        h[:8]=b'EFI PART'
        h[8:24]=pack('IIII',0x10000,92,0,0)
        h[24:56]=pack('QQQQ',lba,1 if backup else count-1,2+sectors,count-2-sectors)
        h[56:72]=uuid.UUID(int=42).bytes_le
        h[72:92]=pack('QIII',table_lba,128,128,zlib.crc32(array))
        if kind=='entry-count': h[80:84]=pack('I',0xffffffff)
        if kind=='entry-size': h[84:88]=pack('I',127)
        if kind=='array-lba': h[72:80]=pack('Q',0xffffffffffffffff)
        if kind=='usable-range': h[40:56]=pack('QQ',2,0xffffffffffffffff)
        h[16:20]=pack('I',zlib.crc32(h[:92]))
        if kind=='both-headers' or (kind=='primary-header' and not backup) or (kind=='backup-header' and backup):h[16]^=1
        if kind=='both-arrays' or (kind=='primary-array' and not backup):array[-1]^=1
        disk[lba*sector:(lba+1)*sector]=h
        disk[table_lba*sector:table_lba*sector+len(array)]=array
    return disk

with tempfile.TemporaryDirectory() as directory:
    path=pathlib.Path(directory)/'disk'
    cases={'valid':(2,0),'primary-header':(2,2),'backup-header':(2,1),
           'primary-array':(2,2),'both-headers':(-1,0),'both-arrays':(-1,0),
           'overlap':(-1,0),'outside':(-1,0),'reversed':(-1,0),
           'duplicate':(-1,0),'null-uuid':(-1,0),'hybrid':(-1,0),
           'conflict':(-1,0),'absent':(0,0),'entry-count':(-1,0),
           'entry-size':(-1,0),'array-lba':(-1,0),'usable-range':(-1,0)}
    for sector in (512,4096):
        for kind,(expected,degraded) in cases.items():
            data=image(sector,kind);path.write_bytes(data)
            subprocess.run([binary,str(path),str(sector),str(expected),str(degraded)],check=True)
            assert path.read_bytes()==data
print('GPT: 36 valid, backup recovery, corrupt metadata and boundary scenarios: PASS')
