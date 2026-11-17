package java.awt.image;

import java.awt.Graphics;
import java.awt.Image;

public class BufferedImage extends Image {
    public static final int TYPE_INT_RGB = 1;
    public static final int TYPE_INT_ARGB = 2;
    private final int width;
    private final int height;
    private final int type;
    private final int[] pixels;
    private final WritableRaster raster;

    public BufferedImage(int width, int height, int type) {
        if (width < 1 || height < 1 || (long) width * height > Integer.MAX_VALUE) {
            throw new IllegalArgumentException("Invalid image dimensions");
        }
        if (type != TYPE_INT_RGB && type != TYPE_INT_ARGB) {
            throw new UnsupportedOperationException("Only integer RGB and ARGB images are supported");
        }
        this.width = width;
        this.height = height;
        this.type = type;
        pixels = new int[width * height];
        raster = new WritableRaster(pixels, width, height);
        if (type == TYPE_INT_RGB) {
            java.util.Arrays.fill(pixels, 0xff000000);
        }
    }

    public WritableRaster getRaster() { return raster; }

    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public int getType() { return type; }
    public int getWidth(ImageObserver observer) { return width; }
    public int getHeight(ImageObserver observer) { return height; }
    public Graphics getGraphics() { return new Graphics(this); }

    private void checkRegion(int x, int y, int w, int h) {
        if (x < 0 || y < 0 || w < 0 || h < 0 || x > width - w || y > height - h) {
            throw new ArrayIndexOutOfBoundsException("Image region is outside its bounds");
        }
    }

    private void checkArray(int[] array, int offset, int stride, int w, int h) {
        long lastRow = offset + (long) (h - 1) * stride;
        long first = Math.min(offset, lastRow);
        long end = Math.max(offset, lastRow) + w;
        if (w > 0 && h > 0 && (first < 0 || end > array.length)) {
            throw new ArrayIndexOutOfBoundsException("Pixel array is too small");
        }
    }

    public int getRGB(int x, int y) {
        checkRegion(x, y, 1, 1);
        return type == TYPE_INT_RGB ? pixels[y * width + x] | 0xff000000 : pixels[y * width + x];
    }

    public void setRGB(int x, int y, int rgb) {
        checkRegion(x, y, 1, 1);
        pixels[y * width + x] = type == TYPE_INT_RGB ? rgb | 0xff000000 : rgb;
    }

    public int[] getRGB(int x, int y, int w, int h, int[] array, int offset, int stride) {
        checkRegion(x, y, w, h);
        if (array == null) {
            long size = h == 0 ? offset : offset + (long) (h - 1) * stride + w;
            if (size < 0 || size > Integer.MAX_VALUE) throw new IllegalArgumentException("Array size");
            array = new int[(int) size];
        }
        checkArray(array, offset, stride, w, h);
        for (int row = 0; row < h; row++) {
            if (type == TYPE_INT_ARGB) {
                System.arraycopy(pixels, (y + row) * width + x, array, offset + row * stride, w);
            } else {
                for (int column = 0; column < w; column++)
                    array[offset + row * stride + column] = getRGB(x + column, y + row);
            }
        }
        return array;
    }

    public void setRGB(int x, int y, int w, int h, int[] array, int offset, int stride) {
        checkRegion(x, y, w, h);
        checkArray(array, offset, stride, w, h);
        for (int row = 0; row < h; row++) {
            for (int column = 0; column < w; column++) {
                setRGB(x + column, y + row, array[offset + row * stride + column]);
            }
        }
    }
}
