import java.util.*;
public class JitMapProbe {
    public static void check() {
        HashMap<String,Integer> map=new HashMap<String,Integer>();
        for(int round=0;round<500;round++) {
            for(int i=0;i<64;i++) map.put(new StringBuilder().append("root.tick.").append(i).toString(),i);
            if(map.size()!=64)throw new RuntimeException("equal keys duplicated: "+map.size());
            int count=0,sum=0;
            for(String key:map.keySet()) {if(++count>64)throw new RuntimeException("iterator cycle");sum+=map.get(key);}
            if(count!=64||sum!=2016)throw new RuntimeException("map iteration "+count+" / "+sum);
        }
        pf.a=true;
        for(int round=0;round<500;round++) {
            pf.a("root");pf.a("tick");pf.b();pf.b();
            java.util.List entries=pf.b("root");
            if(entries.size()<2||entries.size()>3)throw new RuntimeException("game profiler entries: "+entries.size());
        }
        pf.a=false;
        System.out.println("JIT_MAP_PASS");
    }
}
