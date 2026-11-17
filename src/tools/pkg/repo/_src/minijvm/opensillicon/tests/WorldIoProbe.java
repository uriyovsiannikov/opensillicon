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
        System.out.println("JAVA WORLD IO PASS");
    }
}
