import java.io.*;
import java.util.*;
import java.util.zip.*;
import java.text.*;

public class WorldIoProbe implements Runnable {
    private long recursiveResult;

    private static long recurse(int depth) {
        long a = depth, b = depth + 1, c = depth + 2, d = depth + 3;
        long e = depth + 4, f = depth + 5, g = depth + 6, h = depth + 7;
        long i = depth + 8, j = depth + 9, k = depth + 10, l = depth + 11;
        long child = depth == 0 ? 0 : recurse(depth - 1);
        return child + a + b + c + d + e + f + g + h + i + j + k + l;
    }

    public void run() { recursiveResult = recurse(384); }
    private static byte[] readAll(InputStream input) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int count;
        while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
        input.close();
        return output.toByteArray();
    }

    private static String hex(byte[] bytes) {
        StringBuilder output = new StringBuilder();
        char[] digits = "0123456789abcdef".toCharArray();
        for (byte value : bytes) output.append(digits[(value & 255) >>> 4]).append(digits[value & 15]);
        return output.toString();
    }

    public static void main(String[] args) throws Exception {
        if (args.length > 0 && Runtime.getRuntime().maxMemory() != Long.parseLong(args[0]))
            throw new AssertionError("Configured heap size");
        if (Runtime.getRuntime().maxMemory() < Runtime.getRuntime().totalMemory())
            throw new AssertionError("Heap limit");
        java.nio.ByteBuffer raw = java.nio.ByteBuffer.allocate(64).order(java.nio.ByteOrder.nativeOrder());
        raw.position(4);
        java.nio.IntBuffer view = raw.slice().order(java.nio.ByteOrder.nativeOrder()).asIntBuffer();
        view.position(2);
        view.put(new int[]{0, 0x12345678, -1, 0x10203040, 0}, 1, 3);
        if (view.position() != 5 || raw.getInt(12) != 0x12345678 || raw.getInt(16) != -1 ||
                raw.getInt(20) != 0x10203040)
            throw new AssertionError("Bulk integer view write");
        view.position(2);
        int[] integers = new int[5];
        view.get(integers, 1, 3);
        if (!Arrays.equals(integers, new int[]{0, 0x12345678, -1, 0x10203040, 0}) || view.position() != 5)
            throw new AssertionError("Bulk integer view read");
        try {
            view.asReadOnlyBuffer().put(new int[1]);
            throw new AssertionError("Read-only integer view write");
        } catch (java.nio.ReadOnlyBufferException expected) {}
        byte[] shortStorage = new byte[32];
        Arrays.fill(shortStorage, (byte) 0x5a);
        java.nio.ByteBuffer shortBytes = java.nio.ByteBuffer.wrap(shortStorage).order(java.nio.ByteOrder.nativeOrder());
        shortBytes.position(4);
        shortBytes.limit(20);
        java.nio.ShortBuffer shorts = shortBytes.asShortBuffer();
        shorts.position(1);
        shorts.put(new short[]{0, 0x1234, -1, 0x2345, 0}, 1, 3);
        if (shorts.position() != 4 || shortBytes.position() != 4 || shortBytes.getShort(6) != 0x1234 ||
                shortBytes.getShort(8) != -1 || shortBytes.getShort(10) != 0x2345 ||
                shortBytes.getShort(4) != 0x5a5a || shortBytes.getShort(12) != 0x5a5a)
            throw new AssertionError("Short view bulk write boundaries");
        shorts.position(1);
        short[] shortValues = {7, 7, 7, 7, 7};
        shorts.get(shortValues, 1, 3);
        if (!Arrays.equals(shortValues, new short[]{7, 0x1234, -1, 0x2345, 7}) || shorts.position() != 4)
            throw new AssertionError("Short view bulk read boundaries");
        File cleanPath = new File("/tmp/minijvm-check/region/r.0.0.mcr");
        if (!cleanPath.getAbsolutePath().equals("/tmp/minijvm-check/region/r.0.0.mcr") ||
                !cleanPath.getCanonicalPath().equals(cleanPath.getAbsolutePath()) ||
                !new File("/tmp/minijvm-check//region/r.0.0.mcr").equals(cleanPath))
            throw new AssertionError("Normalized file paths");
        WorldIoProbe recursive = new WorldIoProbe();
        Thread thread = new Thread(recursive);
        thread.start();
        thread.join();
        if (recursive.recursiveResult != 12L * 384 * 385 / 2 + 66 * 385)
            throw new AssertionError("Recursive thread stack");
        File path = new File("/tmp/minijvm-world-io-test.dat");
        if (path.exists() && !path.delete()) throw new AssertionError("Cannot clear I/O fixture");
        RandomAccessFile file = new RandomAccessFile(path, "rw");
        if (file.length() != 0 || file.getFilePointer() != 0) throw new AssertionError("New file");
        file.writeInt(0x12345678);
        file.seek(10000);
        file.writeLong(0x0123456789abcdefL);
        if (file.length() != 10008 || file.getFilePointer() != 10008) throw new AssertionError("Position");
        file.seek(4);
        file.setLength(12288);
        if (file.length() != 12288 || file.getFilePointer() != 4) throw new AssertionError("Extend");
        file.seek(0);
        if (file.readInt() != 0x12345678) throw new AssertionError("Header");
        file.close();
        file = new RandomAccessFile(path, "rw");
        if (file.length() != 12288) throw new AssertionError("Reopen truncated data");
        file.seek(10000);
        if (file.readLong() != 0x0123456789abcdefL) throw new AssertionError("Long value");
        file.seek(file.length());
        if (file.read() != -1 || file.read(new byte[0]) != 0) throw new AssertionError("EOF");
        file.close();
        try {
            file.seek(0);
            throw new AssertionError("Closed file seek");
        } catch (IOException expected) {}
        System.out.println("JAVA RANDOM FILE PASS");

        byte[] payload = new byte[65537];
        for (int i = 0; i < payload.length; i++) payload[i] = (byte) (i * 73 ^ i >>> 8);
        for (int gzip = 0; gzip < 2; gzip++) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DeflaterOutputStream output = gzip == 1 ? new GZIPOutputStream(bytes, 7) : new DeflaterOutputStream(bytes);
            output.write(payload, 0, 311);
            output.write(payload, 311, payload.length - 311);
            output.finish();
            output.finish();
            output.close();
            byte[] encoded = bytes.toByteArray();
            InputStream input = gzip == 1 ? new GZIPInputStream(new ByteArrayInputStream(encoded), 3) :
                new InflaterInputStream(new ByteArrayInputStream(encoded));
            if (!Arrays.equals(payload, readAll(input))) throw new AssertionError("Compression roundtrip");
            input = gzip == 1 ? new GZIPInputStream(WorldIoProbe.class.getResourceAsStream("/reference.gz")) :
                new InflaterInputStream(WorldIoProbe.class.getResourceAsStream("/reference.zlib"));
            if (!Arrays.equals(payload, readAll(input))) throw new AssertionError("Reference decompression");
            input = gzip == 1 ? new GZIPInputStream(new ByteArrayInputStream(encoded)) :
                new InflaterInputStream(new ByteArrayInputStream(encoded));
            if (input.skip(10000) != 10000 || input.read() != (payload[10000] & 255))
                throw new AssertionError("Compressed skip");
            input.close();
            if (gzip == 1) {
                encoded[encoded.length - 1] ^= 1;
                try {
                    readAll(new GZIPInputStream(new ByteArrayInputStream(encoded)));
                    throw new AssertionError("Invalid gzip trailer accepted");
                } catch (IOException expected) {}
            }
        }
        ByteArrayOutputStream small = new ByteArrayOutputStream();
        GZIPOutputStream singles = new GZIPOutputStream(small, 7);
        for (int i = 0; i < payload.length; i++) {
            if (i % 257 == 0) singles.write(payload, i, 1);
            else singles.write(payload[i]);
            if (i == 9000) singles.flush();
        }
        singles.close();
        GZIPInputStream singleInput = new GZIPInputStream(new ByteArrayInputStream(small.toByteArray()), 3);
        for (int i = 0; i < payload.length; i++) {
            int value;
            if (i % 257 == 0) {
                byte[] one = new byte[1];
                if (singleInput.read(one) != 1) throw new AssertionError("Mixed read length");
                value = one[0] & 255;
            } else value = singleInput.read();
            if (value != (payload[i] & 255)) throw new AssertionError("Mixed single-byte compression");
        }
        if (singleInput.read() != -1 || singleInput.available() != 0)
            throw new AssertionError("Buffered decompression EOF");
        singleInput.close();
        try {
            singleInput.read();
            throw new AssertionError("Read after compressed close");
        } catch (IOException expected) {}
        small = new ByteArrayOutputStream();
        DeflaterOutputStream compressor = new DeflaterOutputStream(small);
        compressor.write("Minecraft".getBytes("UTF-8"));
        compressor.close();
        System.out.println("JAVA ZLIB HEX " + hex(small.toByteArray()));
        Inflater inflater = new Inflater();
        inflater.setInput(small.toByteArray());
        byte[] plain = new byte[32];
        int size = inflater.inflate(plain);
        if (!inflater.finished() || !"Minecraft".equals(new String(plain, 0, size, "UTF-8")))
            throw new AssertionError("Inflater API");
        inflater.end();
        small = new ByteArrayOutputStream();
        GZIPOutputStream gzip = new GZIPOutputStream(small);
        gzip.write("Minecraft".getBytes("UTF-8"));
        gzip.close();
        System.out.println("JAVA GZIP HEX " + hex(small.toByteArray()));
        try {
            readAll(new GZIPInputStream(new ByteArrayInputStream(new byte[]{0})));
            throw new AssertionError("Invalid gzip header");
        } catch (IOException expected) {}
        DateFormat format = new SimpleDateFormat("yyyy-MM-dd_HH.mm.ss");
        ((SimpleDateFormat) format).setTimeZone(TimeZone.getTimeZone("GMT"));
        if (!"1970-01-01_00.00.00".equals(format.format(new Date(0))))
            throw new AssertionError("Date format");
        Random random = new Random(0);
        if (!random.nextBoolean()) throw new AssertionError("Random boolean");
        random.setSeed(0);
        double gaussian = random.nextGaussian();
        random.setSeed(0);
        if (Math.abs(gaussian - 0.8025330637390305) > 1e-9 || gaussian != random.nextGaussian())
            throw new AssertionError("Random Gaussian");
        if (java.awt.Color.HSBtoRGB(0, 1, 1) != 0xffff0000 ||
                java.awt.Color.HSBtoRGB(1f / 3, 1, 1) != 0xff00ff00 ||
                java.awt.Color.HSBtoRGB(2f / 3, 1, 1) != 0xff0000ff ||
                java.awt.Color.getHSBColor(0, 0, 0.5f).getRGB() != 0xff808080)
            throw new AssertionError("HSB colors");
        java.awt.image.BufferedImage image = new java.awt.image.BufferedImage(2, 1,
                java.awt.image.BufferedImage.TYPE_INT_ARGB);
        int[] pixels = ((java.awt.image.DataBufferInt) image.getRaster().getDataBuffer()).getData();
        pixels[0] = 0x80123456;
        if (image.getRGB(0, 0) != 0x80123456) throw new AssertionError("Shared ARGB raster");
        image = new java.awt.image.BufferedImage(2, 1, java.awt.image.BufferedImage.TYPE_INT_RGB);
        pixels = ((java.awt.image.DataBufferInt) image.getRaster().getDataBuffer()).getData();
        pixels[0] = 0x00123456;
        if (image.getRGB(0, 0) != 0xff123456 || image.getRGB(0, 0, 1, 1, null, 0, 1)[0] != 0xff123456)
            throw new AssertionError("Opaque RGB raster");
        if (!path.delete()) throw new AssertionError("Cannot remove I/O fixture");
        File original = new File("/tmp/minijvm-rename-test.dat");
        File renamed = new File("/tmp/minijvm-rename-moved.dat");
        original.delete(); renamed.delete();
        FileOutputStream fileOutput = new FileOutputStream(original);
        fileOutput.write(new byte[0]);
        fileOutput.write(payload); fileOutput.close();
        if (!original.renameTo(renamed) || original.exists() || !renamed.exists() ||
                !Arrays.equals(payload, readAll(new FileInputStream(renamed))))
            throw new AssertionError("File rename preserved data");
        if (!renamed.renameTo(renamed)) throw new AssertionError("Same-path rename");
        FileOutputStream sentinel = new FileOutputStream(original);
        sentinel.write(42); sentinel.close();
        if ("OpenSillicon".equals(System.getProperty("os.name")) &&
                (renamed.renameTo(original) || original.length() != 1 || !renamed.exists()))
            throw new AssertionError("Rename destination preservation");
        if (!original.delete() || !renamed.delete()) throw new AssertionError("Rename fixture cleanup");
        System.out.println("JAVA WORLD IO PASS");
    }
}
