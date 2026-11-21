import java.io.*;
public class JitFileProbe {
    public static void main(String[] args) throws Exception {
        File path=new File("/tmp/raf-probe.bin");
        RandomAccessFile f=new RandomAccessFile(path,"rw");
        f.setLength(0);
        long start=System.currentTimeMillis();
        for(int i=0;i<2048;i++)f.writeInt(i^0x89abcdef);
        long elapsed=System.currentTimeMillis()-start;
        if(f.getFilePointer()!=8192||f.length()!=8192)throw new RuntimeException("position/length");
        f.seek(0);
        for(int i=0;i<2048;i++)if(f.readInt()!=(i^0x89abcdef))throw new RuntimeException("header content");
        f.seek(0);f.writeShort(0x8123);f.writeChar(0xfedc);f.writeLong(0x8123456789abcdefL);
        f.writeFloat(-1.25f);f.writeDouble(-123.125);
        f.seek(0);
        if(f.readUnsignedShort()!=0x8123||f.readChar()!=0xfedc||f.readLong()!=0x8123456789abcdefL||f.readFloat()!=-1.25f||f.readDouble()!=-123.125)throw new RuntimeException("primitive endian");
        f.seek(8192);f.writeLong(Long.MIN_VALUE);
        RandomAccessFile reader=new RandomAccessFile(path,"r");reader.seek(8192);
        if(reader.readLong()!=Long.MIN_VALUE)throw new RuntimeException("write visibility");reader.close();
        f.close();
        try{f.writeInt(3);throw new RuntimeException("closed write accepted");}catch(IOException expected){}
        f=new RandomAccessFile(path,"r");
        try{f.writeInt(3);throw new RuntimeException("read-only write accepted");}catch(IOException expected){}
        f.close();
        for(String mode:new String[]{"rws","rwd"}) {f=new RandomAccessFile(path,mode);f.seek(0);f.writeInt(123);f.seek(0);if(f.readInt()!=123)throw new RuntimeException("sync mode");f.close();}
        path.delete();
        System.out.println("FILE_PROBE_PASS elapsed_ms="+elapsed);
    }
}
