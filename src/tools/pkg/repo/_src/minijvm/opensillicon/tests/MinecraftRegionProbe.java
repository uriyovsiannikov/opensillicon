import java.io.*;
import java.util.Arrays;
import java.util.Random;

/* Uses the original Minecraft 1.0.0 RegionFile (obfuscated name: as).
 * Supply a locally owned client.jar as build.sh's second argument. */
public class MinecraftRegionProbe {
    public static void main(String[] args) throws Exception {
        File path = new File("/tmp/minijvm-region-check.mcr");
        if (path.exists() && !path.delete()) throw new IOException("Cannot remove fixture");
        as region = new as(path);
        byte[] data = new byte[24576];
        new Random(1234).nextBytes(data);
        for (int i = 0; i < 256; i++) {
            DataOutputStream output = region.b(i % 32, i / 32);
            output.writeInt(i);
            output.write(data);
            output.close();
        }
        region.a();
        region = new as(path);
        byte[] decoded = new byte[data.length];
        for (int i = 0; i < 256; i++) {
            DataInputStream input = region.a(i % 32, i / 32);
            if (input == null || input.readInt() != i)
                throw new AssertionError("Chunk header " + i);
            input.readFully(decoded);
            if (!Arrays.equals(data, decoded) || input.read() != -1)
                throw new AssertionError("Chunk payload " + i);
            input.close();
        }
        region.a();
        if (path.length() < 7 * 1024 * 1024) throw new AssertionError("Region allocation");
        if (!path.delete()) throw new IOException("Cannot remove fixture");
        System.out.println("JAVA MINECRAFT REGION PASS");
    }
}
