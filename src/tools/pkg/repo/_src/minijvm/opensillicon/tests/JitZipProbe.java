import java.util.zip.ZipFile;
import java.util.zip.ZipEntry;
import java.util.Enumeration;
import java.io.InputStream;
public class JitZipProbe {
    public static void main(String[] args) throws Exception {
        String path=args[0];
        boolean legacy=args.length>1;
        if(!legacy) {
            ZipFile z=new ZipFile(path);
            if(z.getEntry("missing")!=null)throw new RuntimeException("missing entry");
            ZipEntry directory=z.getEntry("assets/");
            if(directory==null||!directory.isDirectory())throw new RuntimeException("directory");
            ZipEntry empty=z.getEntry("empty");
            if(empty==null||empty.getSize()!=0||z.getInputStream(empty).read()!=-1)throw new RuntimeException("empty entry");
            if(z.getEntry("assets/7.bin").getSize()!=6)throw new RuntimeException("entry size before extraction");
            for(int repeat=0;repeat<2;repeat++) {
                int count=0;Enumeration<? extends ZipEntry> e=z.entries();
                while(e.hasMoreElements()){e.nextElement();count++;}
                if(count!=1502)throw new RuntimeException("enumeration");
            }
            if(z.getInputStream(z.getEntry("assets/7.bin")).read()!='d')throw new RuntimeException("lookup after enumeration");
            z.close();
        }
        long start=System.currentTimeMillis();long sum=0;
        for(int i=0;i<200;i++) {
            ZipFile z=new ZipFile(path);
            ZipEntry e=z.getEntry("assets/"+(i%1500)+".bin");
            if(e==null)throw new RuntimeException("resource not found");
            InputStream in=z.getInputStream(e);int value;
            while((value=in.read())!=-1)sum+=value;
            in.close();z.close();
        }
        System.out.println("ZIP_PROBE_PASS checksum="+sum+" elapsed_ms="+(System.currentTimeMillis()-start));
    }
}
