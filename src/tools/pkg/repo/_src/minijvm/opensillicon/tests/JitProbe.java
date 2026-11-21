public class JitProbe {
    static volatile int progress;
    static class Box { int value=17; }
    static int fieldGet(Box b) { return b.value; }
    static void fieldSet(Box b, int value) { b.value=value; }

    static int mix(int x) {
        for (int i=0; i<300; i++) x=(x*1664525+1013904223) ^ (x>>>13);
        return x;
    }
    static long wide(long x) { return ((x * 0x123456789L) ^ (x >>> 19)) + (x << 7); }
    static double floating(double x) { return x * 1.25 + x / 8.0 - 0.5; }
    static int array(int[] a, int i) { try { return a[i]; } catch (ArrayIndexOutOfBoundsException e) { return -9; } }
    static int choose(int x) { switch(x) { case 0:return 7; case 3:return 11; case 9:return 13; default:return -1; } }
    // Minecraft's large-tree generator (yd.a) allocates int[rows][4].
    static int multiArray(int rows) {
        int[][] tree = new int[rows][4];
        int[][][] volume = new int[2][3][4];
        tree[rows-1][3] = 101;
        volume[1][2][3] = 37;
        int[][] empty = new int[0][4];
        return tree.length + tree[0].length + tree[rows-1][3]
            + volume[1][2][3] + empty.length;
    }
    public static void main(String[] args) throws Exception {
        JitTextProbe.check();
        JitMapProbe.check();
        JitGetterProbe.check();
        for(int n=-128;n<=127;n++) {
            if(Byte.valueOf((byte)n)!=Byte.valueOf((byte)n) || Byte.valueOf((byte)n).intValue()!=n)throw new RuntimeException("Byte cache");
            if(Short.valueOf((short)n)!=Short.valueOf((short)n) || Short.valueOf((short)n).intValue()!=n)throw new RuntimeException("Short cache");
            if(Integer.valueOf(n)!=Integer.valueOf(n) || Integer.valueOf(n).intValue()!=n)throw new RuntimeException("Integer cache");
            if(Long.valueOf(n)!=Long.valueOf(n) || Long.valueOf(n).longValue()!=n)throw new RuntimeException("Long cache");
        }
        if(Short.valueOf((short)300).shortValue()!=300)throw new RuntimeException("entity metadata boxing");
        if(Short.valueOf((short)-32768).shortValue()!=-32768 || Integer.valueOf(Integer.MIN_VALUE).intValue()!=Integer.MIN_VALUE || Long.valueOf(Long.MIN_VALUE).longValue()!=Long.MIN_VALUE)throw new RuntimeException("uncached boxing");
        int[] values={3,5,7};
        Box box=new Box();
        long start=System.currentTimeMillis();
        int hash=0;
        for(int i=0;i<4000;i++) {
            hash ^= mix(i);
            fieldSet(box,i);
            if(fieldGet(box)!=i)throw new RuntimeException("field access");
            try { fieldGet(null); throw new RuntimeException("missing getter NPE"); } catch(NullPointerException expected) {}
            try { fieldSet(null,i); throw new RuntimeException("missing setter NPE"); } catch(NullPointerException expected) {}

            if(multiArray(7)!=149) throw new RuntimeException("multianewarray");
            if(wide(i)!=((i*0x123456789L) ^ ((long)i>>>19))+((long)i<<7)) throw new RuntimeException("long");
            if(floating(i)!=i*1.375-0.5) throw new RuntimeException("double");
            if(array(values,4)!=-9 || array(values,1)!=5) throw new RuntimeException("exception/array");
            if(choose(3)!=11 || choose(9)!=13 || choose(4)!=-1) throw new RuntimeException("switch");
        }
        Thread other=new Thread(new Runnable(){public void run(){progress=1;}});
        other.start();
        for(int i=0;i<10000 && progress==0;i++) hash^=mix(i);
        other.join();
        if(progress!=1) throw new RuntimeException("scheduler");
        System.out.println("JIT_PROBE_PASS hash="+hash+" elapsed_ms="+(System.currentTimeMillis()-start));
    }
}
