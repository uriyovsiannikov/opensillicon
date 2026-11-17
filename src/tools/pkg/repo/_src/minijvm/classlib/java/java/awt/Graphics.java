package java.awt;

import java.awt.image.BufferedImage;
import java.awt.image.ImageObserver;

public class Graphics {
    private Component target;
    private BufferedImage image;
    private Color color = Color.BLACK;

    Graphics(Component target) {
        this.target = target;
    }

    public Graphics(BufferedImage image) {
        this.image = image;
    }

    public void setColor(Color color) {
        if (color != null) {
            this.color = color;
        }
    }

    public void fillRect(int x, int y, int width, int height) {
        if (image != null) {
            int right = (int) Math.min(image.getWidth(), (long) x + width);
            int bottom = (int) Math.min(image.getHeight(), (long) y + height);
            for (int row = Math.max(0, y); row < bottom; row++) {
                for (int column = Math.max(0, x); column < right; column++) {
                    image.setRGB(column, row, composite(color.getRGB(), image.getRGB(column, row)));
                }
            }
        }
        Component component = target;
        if (component != null && width > 0 && height > 0) {
            long peer = component.peer;
            if (peer != 0) {
                X11Peer.fillRect(peer, color.getRGB(), x, y, width, height);
            }
        }
    }

    private int composite(int source, int destination) {
        int sourceAlpha = source >>> 24;
        int destinationAlpha = destination >>> 24;
        int remainingAlpha = (destinationAlpha * (255 - sourceAlpha) + 127) / 255;
        int alpha = sourceAlpha + remainingAlpha;
        if (alpha == 0) return 0;
        int result = alpha << 24;
        for (int shift = 0; shift <= 16; shift += 8) {
            int channel = (((source >>> shift) & 255) * sourceAlpha +
                    ((destination >>> shift) & 255) * remainingAlpha + alpha / 2) / alpha;
            result |= channel << shift;
        }
        return result;
    }

    public void drawString(String text, int x, int y) {
        if (text.length() == 0) return;
        if (image == null) {
            throw new UnsupportedOperationException("Text drawing requires a BufferedImage");
        }
        byte[] encoded;
        try {
            encoded = text.getBytes("ISO-8859-1");
        } catch (java.io.UnsupportedEncodingException failure) {
            throw new IllegalStateException(failure);
        }
        int[] mask = X11Peer.renderText(encoded);
        if (mask == null) throw new AWTError("Cannot load the X11 fixed font");
        int width = mask[0];
        int height = mask[1];
        int top = y - mask[2];
        for (int row = 0; row < height; row++) {
            for (int column = 0; column < width; column++) {
                int dx = x + column;
                int dy = top + row;
                if (mask[3 + row * width + column] != 0 && dx >= 0 && dy >= 0 &&
                        dx < image.getWidth() && dy < image.getHeight()) {
                    image.setRGB(dx, dy, composite(color.getRGB(), image.getRGB(dx, dy)));
                }
            }
        }
    }

    public boolean drawImage(Image source, int x, int y, ImageObserver observer) {
        if (source == null) return true;
        if (!(source instanceof BufferedImage) || image == null) {
            throw new UnsupportedOperationException("Image copying requires two BufferedImages");
        }
        BufferedImage input = (BufferedImage) source;
        int left = Math.max(0, x);
        int top = Math.max(0, y);
        int right = (int) Math.min(image.getWidth(), (long) x + input.getWidth());
        int bottom = (int) Math.min(image.getHeight(), (long) y + input.getHeight());
        // Copy the source first to preserve overlapping self-draws.
        int width = Math.max(0, right - left);
        int height = Math.max(0, bottom - top);
        if (width == 0 || height == 0) return true;
        int[] pixels = input.getRGB(left - x, top - y, width, height, null, 0, width);
        for (int row = top; row < bottom; row++) {
            for (int column = left; column < right; column++) {
                image.setRGB(column, row, composite(pixels[(row - top) * width + column - left],
                        image.getRGB(column, row)));
            }
        }
        return true;
    }

    public void dispose() {
        target = null;
        image = null;
    }
}
