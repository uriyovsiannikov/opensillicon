import java.util.Random;
/** Exercise Minecraft 1.0.0's actual octave and Perlin noise implementations. */
public class JitTerrainProbe {
    public static void main(String[] args) throws Exception {
        JitProbe.main(args);
        eb noise = new eb(new Random(12345), 16);
        double[] samples = null;
        double sum = 0;
        for (int chunk = 0; chunk < 512; chunk++)
            samples = noise.a(samples, chunk * 4, 0, -chunk * 4, 5, 17, 5, 0.125, 0.25, 0.125);
        long start = System.currentTimeMillis();
        for (int chunk = 0; chunk < 4096; chunk++) {
            samples = noise.a(samples, chunk * 4, 0, -chunk * 4, 5, 17, 5, 0.125, 0.25, 0.125);
            for (int i = 0; i < samples.length; i++) sum += samples[i];
        }
        System.out.println("TERRAIN_PROBE sum=" + sum + " elapsed_ms=" + (System.currentTimeMillis() - start));
        if (Double.isNaN(sum) || Double.isInfinite(sum)) throw new RuntimeException("noise");
    }
}
