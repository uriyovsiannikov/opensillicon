public class JitGetterProbe {
    static class Values {
        int i=-1234567;long l=0x1234567887654321L;
        byte b=-125;short s=-32000;char c=60000;
        float f=-12.5f;double d=-1234.25;Object ref=new Object();
        volatile int volatileValue=37;
        int integer(){return i;} long wide(){return l;} byte small(){return b;}
        short narrow(){return s;} char character(){return c;}
        float floating(){return f;} double decimal(){return d;} Object reference(){return ref;}
        int vol(){return volatileValue;} synchronized int locked(){return i;}
    }
    static class Child extends Values {int integer(){return 99;}}
    static long sample(Values v, Values child) {
        long checksum=0;
        for(int n=0;n<1000;n++) {
            if(v.integer()!=-1234567||child.integer()!=99||v.wide()!=0x1234567887654321L||
                v.small()!=-125||v.narrow()!=-32000||v.character()!=60000||v.floating()!=-12.5f||
                v.decimal()!=-1234.25||v.reference()!=v.ref||v.locked()!=-1234567)
                throw new RuntimeException("getter value/dispatch");
            v.volatileValue=n;if(v.vol()!=n)throw new RuntimeException("volatile getter");
            checksum+=v.integer()+child.integer()+v.character();
        }
        return checksum;
    }
    static void check() throws Exception {
        Values v=new Values(),child=new Child();
        long start=System.currentTimeMillis(),checksum=0;
        for(int n=0;n<1000;n++)checksum+=sample(v,child);
        try {Values missing=null;missing.integer();throw new RuntimeException("getter null");}catch(NullPointerException expected){}
        if(checksum!=-1174468000000L)throw new RuntimeException("getter checksum: "+checksum);
        System.out.println("GETTER_PROBE_PASS checksum="+checksum+" elapsed_ms="+(System.currentTimeMillis()-start));
    }
    public static void main(String[] args) throws Exception {check();}
}
