public class JitTextProbe {
    static String text(int n, long l) {
        return new StringBuilder(1).append("chunk ").append(n).append(' ').append(l).toString();
    }
    public static void check() {
        int[] ints={Integer.MIN_VALUE,-10000,-100,-10,-1,0,1,9,10,99,100,10000,Integer.MAX_VALUE};
        long[] longs={Long.MIN_VALUE,-10000000000L,-100,-1,0,1,100,10000000000L,Long.MAX_VALUE};
        for(int n:ints) for(long l:longs) {
            String expected="chunk ".concat(Integer.toString(n)).concat(" ").concat(Long.toString(l));
            if(!text(n,l).equals(expected)) throw new RuntimeException("text mismatch");
            StringBuilder b=new StringBuilder(1).append(n);
            String first=b.toString();
            b.append(l);
            if(!first.equals(Integer.toString(n)))throw new RuntimeException("string mutated");
            if(!b.toString().equals(Integer.toString(n).concat(Long.toString(l))))throw new RuntimeException("append mismatch");
        }
    }
    public static void main(String[] args) {
        check();
        long start=System.currentTimeMillis();long checksum=0;
        for(int i=0;i<40000;i++) {
            String s=text(i-20000,(long)i*123456789L);
            checksum+=s.length()+s.charAt(s.length()-1);
        }
        System.out.println("TEXT_PROBE_PASS checksum="+checksum+" elapsed_ms="+(System.currentTimeMillis()-start));
    }
}
