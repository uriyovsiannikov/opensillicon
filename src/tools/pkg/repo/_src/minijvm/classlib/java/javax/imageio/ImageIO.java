package javax.imageio;

import java.awt.image.BufferedImage;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;

public final class ImageIO {
    private ImageIO() {}

    public static BufferedImage read(URL url) throws IOException {
        if (url == null) throw new IllegalArgumentException("url is null");
        InputStream input = url.openStream();
        try {
            return read(input);
        } finally {
            input.close();
        }
    }

    public static BufferedImage read(InputStream input) throws IOException {
        if (input == null) throw new IllegalArgumentException("input is null");
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        byte[] chunk = new byte[4096];
        int count;
        while ((count = input.read(chunk)) != -1) {
            if (count == 0) {
                int value = input.read();
                if (value == -1) break;
                bytes.write(value);
            } else {
                bytes.write(chunk, 0, count);
            }
            if (bytes.size() > 64 * 1024 * 1024) throw new IOException("Image input exceeds 64 MiB");
        }
        byte[] encoded = bytes.toByteArray();
        if (encoded.length < 8 || encoded[0] != (byte) 137 || encoded[1] != 80 ||
                encoded[2] != 78 || encoded[3] != 71 || encoded[4] != 13 ||
                encoded[5] != 10 || encoded[6] != 26 || encoded[7] != 10) {
            return null;
        }
        int[] decoded = decodePNG(encoded);
        if (decoded == null) throw new IOException("Invalid or unsupported PNG image");
        BufferedImage image = new BufferedImage(decoded[0], decoded[1], BufferedImage.TYPE_INT_ARGB);
        image.setRGB(0, 0, decoded[0], decoded[1], decoded, 2, decoded[0]);
        return image;
    }

    private static native int[] decodePNG(byte[] encoded);
}
